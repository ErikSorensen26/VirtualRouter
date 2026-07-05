// OspfInterface.cpp

#include <VirtualRouter.h>

#include "OspfInterface.h"
#include "ospf/OspfProcess.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/area/FlagManager.h"
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
    : id(id),
      interfaceId(iface.configs.key.getId()),
      interfaceAddress(getIfaceAddr(iface, proc.getAF())),
      process(proc),
      dispatcher(proc.isV3
          ? static_cast<PacketDispatcher*>(new PacketDispatcherV3(*this))
          : static_cast<PacketDispatcher*>(new PacketDispatcherV2(*this))),
      area(process.insureArea(id.area)),
      flags(*this),
      lsaFlags(*this),
      ntable(*this),
      tmgr(*this),
      iface(iface),
      baseConfigs(dispatcher->getConfigs()),
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
    uint32_t pid = process.getProcId();

    // Tear down all neighbors and expire originated LSAs
    tmgr.stopHello();
    for (auto& [rid, nbr] : ntable.neighbors)
        nbr.setState(Neighbor::State::DOWN);

    // Tell the originator to withdraw this interface's contributions
    // (removes the network LSA if DR, removes router link, rebuilds router LSA)
    area.getOriginator().updateInterface(interfaceId);

    delete dispatcher;
}

void OspfInterface::calculateCost()
{
    uint16_t oldCost = cost;
    uint16_t newCost{0};

    auto configuredCost = configs.get<config::OspfInterface::COST>();
    if (configuredCost.hasValue())
    {
        newCost = configuredCost.load();
    }
    else
    {
        uint32_t referenceBw = process.getConfigs().get<config::Ospf::REFERENCE_BANDWIDTH>().load();
        uint32_t interfaceBw = iface.configs.getBandwidth();
        newCost = static_cast<uint16_t>(referenceBw / interfaceBw);
    }
    
    cost = newCost;

    if (oldCost != newCost)
    {
        area.getOriginator().updateInterface(interfaceId);
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

    if (candDr == getArea().process().getRouterId())
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

    if (candBdr == getArea().process().getRouterId())
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
    struct Candidate {
        uint32_t rid;
        uint8_t priority;
        uint32_t claimedDr;
        uint32_t claimedBdr;
    };

    uint32_t selfRid  = getArea().process().getRouterId();
    uint8_t  selfPrio = configs.get<config::OspfInterface::PRIORITY>().load();

    const uint32_t prevDr = dr.rid.load(std::memory_order_relaxed);
    const uint32_t prevBdr = dr.rid.load(std::memory_order_relaxed);
    const bool wasDr  = (prevDr  == selfRid);
    const bool wasBdr = (prevBdr == selfRid);

    std::vector<Candidate> eligible;
    eligible.reserve(ntable.neighbors.size() + 1);

    if (selfPrio > 0)
        eligible.push_back({ selfRid, selfPrio, prevDr, prevBdr });

    for (const auto& [rid, nbr] : ntable.neighbors)
    {
        if (nbr.getState() < Neighbor::State::TWOWAY)
            continue;

        uint8_t prio = nbr.priority.load(std::memory_order_relaxed);
        if (prio == 0)
            continue;

        eligible.push_back({
            rid, prio,
            nbr.dr.load(std::memory_order_relaxed),
            nbr.bdr.load(std::memory_order_relaxed)
        });
    }

    if (eligible.empty())
    {
        setDr(0);
        setBdr(0);
        isDr.store(false, std::memory_order_release);
        isBdr.store(false, std::memory_order_release);
        if (prevDr != 0 || prevBdr != 0)
            area.getOriginator().updateInterface(interfaceId);
        return;
    }

    // Higher priority wins; tie-break by higher RID
    auto best = [](const Candidate& a, const Candidate& b) -> bool
    {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.rid > b.rid;
    };

    auto runElection = [&]() -> std::pair<uint32_t,uint32_t>
    {
        // Step 1: Elect BDR
        const Candidate* declaredBdr = nullptr;
        const Candidate* fallbackBdr = nullptr;

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
        const Candidate* declaredDr = nullptr;
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
    isDr.store(amDr, std::memory_order_release);
    isBdr.store(amBdr, std::memory_order_release);

    // If DR/BDR changed, trigger neighbor transitions and router LSA rebuild
    if (drChanged || bdrChanged)
    {
        // Neighbors that were TWOWAY and are now DR or BDR eligible need EXSTART
        for (auto& [rid, nbr] : ntable.neighbors)
        {
            if (nbr.getState() < Neighbor::State::TWOWAY)
                continue;
            const bool adjacencyNeeded =
                amDr || amBdr || rid == finalDr || rid == finalBdr;

            if (adjacencyNeeded && nbr.getState() == Neighbor::State::TWOWAY)
                nbr.setState(Neighbor::State::EXSTART);
            else if (!adjacencyNeeded && nbr.getState() > Neighbor::State::TWOWAY)
                nbr.setState(Neighbor::State::TWOWAY);
        }

        area.getOriginator().updateInterface(interfaceId);
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
        helloTime = std::chrono::seconds(1) / helloMultiplier.load();
        deadTime = std::chrono::seconds(1);
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

        helloTime = std::chrono::seconds(ht);
        deadTime = std::chrono::seconds(dt);
    }
}

void OspfInterface::syncNetworkType()
{
    auto ntype = getConfigs().get<config::OspfInterface::NETWORK>().load();

    syncTimers();
    isMulticast.store(
        ntype == config::ospf::NetworkType::BROADCAST ||
        ntype == config::ospf::NetworkType::POINT_TO_MULTIPOINT_BROADCAST ||
        ntype == config::ospf::NetworkType::POINT_TO_POINT,
        std::memory_order_release
    );
    getNTable().syncUnicast();
}

void OspfInterface::syncDigestKey()
{
    baseConfigs.get<config::OspfInterfaceBase::MESSAGE_DIGEST_KEYS>().withRead([this](const auto& keys)
    {
        if (!keys.empty())
        {
            const auto& last = keys.back();
            authKey = utils::readU128(std::get<1>(last).value.data());
            authKeyId = std::get<0>(last);
        }
        else
        {
            authKey.reset();
            authKeyId.reset();
        }
    });
}

void OspfInterface::setPassiveMode(bool passive)
{
    configs.get<config::OspfInterface::PASSIVE>().load();
    if (passive)
    {
        for (auto it = ntable.neighbors.begin(); it != ntable.neighbors.end();)
        {
            tmgr.cancleInactiveTimer(it->second);
            auto next = std::next(it);
            Neighbor& nbr = it->second;
            nbr.setState(Neighbor::State::DOWN);
            it = next;
        }
        tmgr.stopHello();
    }
    else
    {
        tmgr.startHello();
    }
}

Area& OspfInterface::getArea()
{
    return area;
}
}

} // namespace routing
