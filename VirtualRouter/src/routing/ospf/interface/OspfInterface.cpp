// OspfInterface.cpp

#include <VirtualRouter.h>

#include "OspfInterface.h"
#include "ospf/OspfProcess.h"
#include "ospf/area/IntraOriginator.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/transmission/PacketDispatcher.h"
#include "ospf/ospfv2/transmission/PacketDispatcherV2.h"
#include "ospf/ospfv3/transmission/PacketDispatcherV3.h"
#include "interface/Interface.h"
#include "ospf/OspfTypes.hpp"

namespace routing
{

auto getIfaceAddr(interface::Interface& iface, types::AddressFamily af) -> types::IPPrefix
{
    if (af == types::AddressFamily::IPv4)
    {
        auto pfx = iface.configs.ipv4.getPrimaryPrefix(true);
        return types::IPPrefix(pfx.addr, pfx.prefixLength, true);
    }
    else
    {
        auto pfx = iface.configs.ipv6.getLocalPrefix();
        return types::IPPrefix(pfx.addr, pfx.prefixLength, true);
    }
}

namespace ospf
{
OspfInterface::OspfInterface(OspfProcess& proc, interface::Interface& iface, const OspfInterfaceId& id)
    : area(proc.insureArea(id.area)),
      id(id),
      interfaceId(iface.configs.key.getId()),
      interfaceAddress(getIfaceAddr(iface, proc.af)),
      iface(iface),
      dispatcher(proc.isV3
          ? *static_cast<PacketDispatcher*>(new PacketDispatcherV3(*this))
          : *static_cast<PacketDispatcher*>(new PacketDispatcherV2(*this))),
      tmgr(*this, proc.schedulerMgr.ref()),
      ntable(*this, tmgr),
      flags(area.flags),
      lsaFlags(area.flags),
      process(proc),
      baseConfigs(dispatcher.getConfigs()),
      configs(baseConfigs.get<config::OspfInterfaceBase::BASE>().get())
{
    configs.context().set(this);
    baseConfigs.context().set(this);

    syncConfigs();
    calculateCost();
    tmgr.startHello();
}

OspfInterface::~OspfInterface()
{
    // Tear down all neighbors and expire originated LSAs
    tmgr.stopHello();

    // Tell the originator to withdraw this interface's contributions
    // (removes the network LSA if DR, removes router link, rebuilds router LSA)
    updateOriginations();

    delete &dispatcher;
}

void OspfInterface::enqueueSyncTimers()
{
    process.scheduler.post([this] {
        syncTimers();
    });
}

void OspfInterface::enqueueSyncNetworkType()
{
    process.scheduler.post([this] {
        syncNetworkType();
    });
}

void OspfInterface::enqueueSyncUnicastNeighbors()
{
    process.scheduler.post([this] {
        ntable.syncUnicast();
    });
}

void OspfInterface::enqueueSyncDemandCircuit()
{
    process.scheduler.post([this] {
        updateOriginations();
        setFloodReduction();
    });
}

void OspfInterface::enqueueSyncPassive()
{
    process.scheduler.post([this] {
        syncPassive();
    });
}

void OspfInterface::enqueueSyncDigestKey()
{
    process.scheduler.post([this] {
        syncDigestKey();
    });
}

void OspfInterface::enqueueSyncPrefixSuppression()
{
    process.scheduler.post([this] {
        updateOriginations();
    });
}

void OspfInterface::calculateCost()
{
    uint16_t oldCost = priv.cost.load(std::memory_order_relaxed);
    uint16_t newCost{0};

    auto configuredCost = configs.get<config::OspfInterface::COST>();
    if (configuredCost.hasValue())
    {
        newCost = configuredCost.load();
    }
    else
    {
        uint32_t referenceBw = process.configs.get<config::Ospf::REFERENCE_BANDWIDTH>().load();
        uint32_t interfaceBw = iface.configs.getBandwidth();
        newCost = static_cast<uint16_t>(referenceBw / interfaceBw);
    }
    
    priv.cost.store(newCost, std::memory_order_relaxed);

    if (oldCost != newCost)
    {
        updateOriginations();
    }
}

bool OspfInterface::setDr(uint32_t candDr)
{
    if (candDr == 0)
    {
        dr.rid.store(0, std::memory_order_release);
        dr.ip.store(0, std::memory_order_release);
        return true;
    }

    if (candDr == process.getRouterId())
    {
        dr.rid.store(candDr, std::memory_order_release);
        dr.ip.store(interfaceAddress.addr, std::memory_order_release);
        return true;
    }

    auto* nbr = ntable.lookup(candDr);
    if (!nbr) return false;

    dr.rid.store(candDr, std::memory_order_release);
    dr.ip.store(nbr->ipAddress.raw, std::memory_order_release);
    return true;
}

bool OspfInterface::setBdr(uint32_t candBdr)
{
    if (candBdr == 0)
    {
        bdr.rid.store(0, std::memory_order_release);
        bdr.ip.store(0, std::memory_order_release);
        return true;
    }

    if (candBdr == process.getRouterId())
    {
        bdr.rid.store(candBdr, std::memory_order_release);
        bdr.ip.store(interfaceAddress.addr, std::memory_order_release);
        return true;
    }

    auto* nbr = ntable.lookup(candBdr);
    if (!nbr) return false;

    bdr.rid.store(candBdr, std::memory_order_release);
    bdr.ip.store(nbr->ipAddress.raw, std::memory_order_release);
    return true;
}

void OspfInterface::election()
{
    // RFC 2328 §9.4 — two-pass DR/BDR election

    uint32_t selfRid  = process.getRouterId();
    uint8_t  selfPrio = configs.get<config::OspfInterface::PRIORITY>().load();

    const uint32_t prevDr = dr.rid.load(std::memory_order_relaxed);
    const uint32_t prevBdr = dr.rid.load(std::memory_order_relaxed);
    const bool wasDr  = (prevDr  == selfRid);
    const bool wasBdr = (prevBdr == selfRid);

    std::vector<DrCandidate> eligible;
    eligible.reserve(ntable.size() + 1);

    if (selfPrio > 0)
        eligible.push_back({ selfRid, selfPrio, prevDr, prevBdr });

    ntable.forEach([&eligible](uint32_t rid, Neighbor& nbr) {
        if (nbr.getState() < Neighbor::State::TWOWAY)
            return;

        uint8_t prio = nbr.priority.load(std::memory_order_relaxed);
        if (prio == 0)
            return;

        eligible.push_back({
            rid, prio,
            nbr.dr.load(std::memory_order_relaxed),
            nbr.bdr.load(std::memory_order_relaxed)
        });
    });

    if (eligible.empty())
    {
        setDr(0);
        setBdr(0);
        priv.isDr.store(false, std::memory_order_release);
        priv.isBdr.store(false, std::memory_order_release);
        if (prevDr != 0 || prevBdr != 0)
            updateOriginations();
        return;
    }

    // Higher priority wins; tie-break by higher RID
    auto best = [](const DrCandidate& a, const DrCandidate& b) -> bool
    {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.rid > b.rid;
    };

    auto runElection = [&]() -> std::pair<uint32_t,uint32_t>
    {
        // Step 1: Elect BDR
        const DrCandidate* declaredBdr = nullptr;
        const DrCandidate* fallbackBdr = nullptr;

        for (const auto& c : eligible)
        {
            if (c.claimedBdr == c.rid)
                continue;
            if (c.claimedBdr == c.rid)
            {
                if (!declaredBdr || best(c, *declaredBdr))
                    declaredBdr = &c;
            }
            if (!fallbackBdr || best(c, *fallbackBdr))
                fallbackBdr = &c;
        }

        const uint32_t newBdr = declaredBdr ? declaredBdr->rid
                              : fallbackBdr ? fallbackBdr->rid
                              : 0;

        // Step 2: Elect DR
        const DrCandidate* declaredDr = nullptr;
        for (const auto& c : eligible)
        {
            if (c.claimedDr == c.rid)
            {
                if (!declaredDr || best(c, *declaredDr))
                    declaredDr = &c;
            }
        }

        // If nobody declares itself DR, the newly elecvted BDR becomes DR.
        uint32_t newDr = declaredDr ? declaredDr->rid : newBdr;

        return {newDr, newBdr};
    };

    auto setSelfClaims = [&](uint32_t asDr, uint32_t asBdr)
    {
        for (auto& c : eligible)
        {
            if (c.rid != selfRid)
                continue;
            c.claimedDr = asDr;
            c.claimedBdr = asBdr;
            return;
        }
    };

    // First pass
    auto [newDr, newBdr] = runElection();

    // Step 4: repeat steps 2 & 3 only if OUR OWN status changed
    if ((newDr == selfRid) != wasDr || (newBdr == selfRid) != wasBdr)
    {
        if (newDr == selfRid)
            setSelfClaims(selfRid, 0);
        else if (newBdr == selfRid)
            setSelfClaims(0, selfRid);
        else
            setSelfClaims(0, 0);

        std::tie(newDr, newBdr) = runElection();
    }

    const uint32_t finalDr = newDr;
    const uint32_t finalBdr = newBdr;

    const bool drChanged  = (finalDr  != prevDr);
    const bool bdrChanged = (finalBdr != prevBdr);

    setDr(finalDr);
    setBdr(finalBdr);

    const bool amDr  = (finalDr  == selfRid);
    const bool amBdr = (finalBdr == selfRid);
    priv.isDr.store(amDr, std::memory_order_release);
    priv.isBdr.store(amBdr, std::memory_order_release);

    // If DR/BDR changed, trigger neighbor transitions and router LSA rebuild
    if (drChanged || bdrChanged)
    {
        // Neighbors that were TWOWAY and are now DR or BDR eligible need EXSTART
        ntable.forEach([amDr, amBdr, finalDr, finalBdr](uint32_t rid, Neighbor& nbr) {
            if (nbr.getState() < Neighbor::State::TWOWAY)
                return;
            const bool adjacencyNeeded =
                amDr || amBdr || rid == finalDr || rid == finalBdr;

            if (adjacencyNeeded && nbr.getState() == Neighbor::State::TWOWAY)
                nbr.setState(Neighbor::State::EXSTART);
            else if (!adjacencyNeeded && nbr.getState() > Neighbor::State::TWOWAY)
                nbr.setState(Neighbor::State::TWOWAY);
        });

        updateOriginations();
    }
}

void OspfInterface::syncConfigs()
{
    opaqueEnabled.store(true, std::memory_order_release);
    syncTimers();
}

void OspfInterface::syncTimers()
{
    auto helloTimer = configs.get<config::OspfInterface::HELLO_INTERVAL>();
    auto helloMultiplier = configs.get<config::OspfInterface::HELLO_MULTIPLIER>();
    auto deadTimer = configs.get<config::OspfInterface::DEAD_INTERVAL>();

    if (helloMultiplier.hasValue())
    {
        priv.helloTime = std::chrono::seconds(1) / helloMultiplier.load();
        priv.deadTime = std::chrono::seconds(1);
        return;
    }
    else
    {
        uint16_t ht;
        uint16_t dt;

        if (helloTimer.hasValue())
        {
            ht = helloTimer.load();
        }
        else
        {
            auto net = configs.get<config::OspfInterface::NETWORK>().load();
            if (net == config::ospf::NetworkType::NON_BROADCAST || net == config::ospf::NetworkType::POINT_TO_MULTIPOINT_BROADCAST || net == config::ospf::NetworkType::POINT_TO_MULTIPOINT)
                ht = OSPF_MU_HELLO_TIME;
            else
                ht = OSPF_HELLO_TIME;
            helloTimer.set(ht);
        }

        if (deadTimer.hasValue())
            dt = deadTimer.load();
        else
        {
            dt = ht * 4;
            deadTimer.set(dt);
        }

        priv.helloTime = std::chrono::seconds(ht);
        priv.deadTime = std::chrono::seconds(dt);
    }
}

void OspfInterface::syncNetworkType()
{
    auto ntype = configs.get<config::OspfInterface::NETWORK>().load();

    syncTimers();
    isMulticast.store(
        ntype == config::ospf::NetworkType::BROADCAST ||
        ntype == config::ospf::NetworkType::POINT_TO_MULTIPOINT_BROADCAST ||
        ntype == config::ospf::NetworkType::POINT_TO_POINT,
        std::memory_order_release
    );
    ntable.syncUnicast();
}

void OspfInterface::syncDigestKey()
{
    baseConfigs.get<config::OspfInterfaceBase::MESSAGE_DIGEST_KEYS>().withRead([this](const auto& keys)
    {
        if (!keys.empty())
        {
            const auto& last = keys.back();
            priv.authKey = utils::readU128(std::get<1>(last).value.data());
            priv.authKeyId = std::get<0>(last);
        }
        else
        {
            priv.authKey.reset();
            priv.authKeyId.reset();
        }
    });
}

void OspfInterface::syncPassive()
{
    bool passive = configs.get<config::OspfInterface::PASSIVE>().load();

    if (passive)
    {
        ntable.resetNeighbors();
        tmgr.stopHello();
    }
    else
    {
        tmgr.startHello();
    }
}

void OspfInterface::setFloodReduction()
{
    const bool enableFloodReduction =
        area.isDcCompatible() && (
            configs.get<config::OspfInterface::FLOOD_REDUCTION>().load() ||
            configs.get<config::OspfInterface::DEMAND_CIRCUIT>().load()
        );

    if (floodReduction != enableFloodReduction)
    {
        floodReduction = enableFloodReduction;
        tmgr.scheduleHello();
        updateOriginations();
    }
}

void OspfInterface::updateOriginations()
{
    area.originator.updateInterface(interfaceId) ;
}

void OspfInterface::resetNeighbors()
{
    ntable.resetNeighbors();
}

void OspfInterface::flushNeighborLsas(Neighbor& nbr)
{
    area.flushNeighborLsas(nbr.routerID);
}

bool OspfInterface::compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const
{
    return area.compareLSASummary(hdr, key);
}

template <typename Policy>
std::optional<Area::Result> OspfInterface::processLsa(IncomingLsaContext& ctx, LsaBody& body)
{
    return area.processLsa<Policy>(ctx, body);
}

template std::optional<Area::Result> OspfInterface::processLsa<PolicyV2>(IncomingLsaContext&, LsaBody&);
template std::optional<Area::Result> OspfInterface::processLsa<PolicyV3>(IncomingLsaContext&, LsaBody&);

void OspfInterface::runAreaDCIntegrityScan()
{
    area.runDCIntegrityScan();
}

const LsdbTable& OspfInterface::getLsdb() const
{
    return area.lsdb;
}

const config::OspfRegistry& OspfInterface::getProcessConfigs() const
{
    return process.configs;
}

const config::OspfAreaRegistry& OspfInterface::getAreaConfigs() const
{
    return area.configs;
}
}
} // namespace routing
