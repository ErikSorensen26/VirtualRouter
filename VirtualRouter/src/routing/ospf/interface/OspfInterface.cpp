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
    : OspfInterface(proc, iface, id, [&]() -> config::OspfGlobalInterfaceRegistry& {
          auto& ifCfgs = iface.configs.getConfigs();
          if (proc.isV3)
          {
            auto& afReg = *ifCfgs.get<config::Interface::OSPFV3>().emplaceBack(proc.procId);
            if (proc.af == types::AddressFamily::IPv4)
                return afReg.get<config::OspfInterfaceAf::IPV4>().get();
            else
                return afReg.get<config::OspfInterfaceAf::IPV6>().get();
          }
          else
          {
              if (proc.af == types::AddressFamily::IPv4)
                  return ifCfgs.get<config::Interface::IP_OSPF>().get();
              else
                  return ifCfgs.get<config::Interface::IPV6_OSPF>().get();
          }
    }())
{}

OspfInterface::OspfInterface(
    OspfProcess& proc,
    interface::Interface& iface,
    const OspfInterfaceId& id,
    const config::OspfGlobalInterfaceRegistry& cfgs
)   : OspfInterfaceBase(proc, id, cfgs),
      interfaceAddress(getIfaceAddr(iface, proc.af)),
      iface(iface),
      globalConfigs(cfgs),
      configs(cfgs.get<config::OspfGlobalInterface::BASE>().get())
{
    configs.context().set(static_cast<OspfInterfaceBase*>(this));
    globalConfigs.context().set(static_cast<OspfInterfaceBase*>(this));
    configsBase.context().set(static_cast<OspfInterfaceBase*>(this));
    globalConfigsBase.context().set(static_cast<OspfInterfaceBase*>(this));

    syncConfigs();
    calculateCost();
    tmgr.startHello();
}

void OspfInterface::enqueueSyncNetworkType(config::ospf::NetworkType ntype)
{
    getScheduler().post([this, ntype] {
        syncNetworkType(ntype);
    });
}

void OspfInterface::enqueueSyncUnicastNeighbors()
{
    getScheduler().post([this] {
        ntable.syncUnicast();
    });
}

void OspfInterface::enqueueSyncDemandCircuit()
{
    getScheduler().post([this] {
        updateOriginations();
        setFloodReduction();
    });
}

void OspfInterface::enqueueSyncPassive(bool passive)
{
    getScheduler().post([this, passive] {
        syncPassive(passive);
    });
}

void OspfInterface::enqueueSyncPrefixSuppression()
{
    getScheduler().post([this] {
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
        uint32_t referenceBw = getProcessConfigs().get<config::Ospf::REFERENCE_BANDWIDTH>().load();
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

void OspfInterface::syncNetworkType(config::ospf::NetworkType ntype)
{
    syncTimers();
    isMulticast.store(
        ntype == config::ospf::NetworkType::BROADCAST ||
        ntype == config::ospf::NetworkType::POINT_TO_MULTIPOINT_BROADCAST ||
        ntype == config::ospf::NetworkType::POINT_TO_POINT,
        std::memory_order_release
    );
    ntable.syncUnicast();
}

void OspfInterface::syncPassive(bool passive)
{
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

void OspfInterface::handleGraceLsaReceived(uint32_t advertisingRouter, const GraceLsaTlv& tlv)
{
    if (!configs.get<config::OspfInterface::GRACEFUL_RESTART_HELPER>().load())
        return;

    OspfInterfaceBase::handleGraceLsaReceived(advertisingRouter, tlv);
}
}
} // namespace routing
