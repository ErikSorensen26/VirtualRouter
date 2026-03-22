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

auto getIfaceAddr(Interface& iface, AddressFamily af) -> IPPrefix
{
    if (af == AddressFamily::IPv4)
    {
        auto pfx = iface.configs.ipv4.getPrimaryPrefix();
        return IPPrefix(pfx.addr, pfx.prefixLength, true);
    }
    else
    {
        auto pfx = iface.configs.ipv6.getLocalPrefix();
        return IPPrefix(pfx.addr, pfx.prefixLength, true);
    }
}

namespace OSPF
{
OspfInterface::OspfInterface(OspfProcess& proc, Interface& iface, Config::Reference<Config::OspfInterfaceBaseRegistry>& configs, const OspfInterfaceId& id)
    : id(id),
      interfaceId(iface.configs.key),
      interfaceAddress(getIfaceAddr(iface, proc.getAF())),
      dispatcher(proc.isV3
          ? static_cast<PacketDispatcher*>(new PacketDispatcherV3(*this, configs))
          : static_cast<PacketDispatcher*>(new PacketDispatcherV2(*this, configs))),
      process(proc),
      area(process.insureArea(id.area)),
      flags(*this),
      lsaFlags(*this),
      ntable(*this),
      tmgr(*this),
      iface(iface),
      configs(configs->get<Config::OspfInterfaceBase::BASE>().local()),
      baseConfigs(configs)
{
    configs->context().set(this);
    baseConfigs->context().set(this);

    syncConfigs();
    calculateCost();
    tmgr.startHello();
}

OspfInterface::~OspfInterface()
{
    uint32_t pid = process.getProcId();
    AddressFamily af = process.getAF();

    // Tear down all neighbors and expire originated LSAs
    tmgr.stopHello();
    for (auto& [rid, nbr] : ntable.neighbors)
        nbr.setState(Neighbor::State::DOWN);

    // Tell the originator to withdraw this interface's contributions
    // (removes the network LSA if DR, removes router link, rebuilds router LSA)
    area.getOriginator().updateInterface(interfaceId);

    if (iface.ospfInterfaceList.find(pid) != iface.ospfInterfaceList.end())
    {
        if (af == AddressFamily::IPv4)
            iface.ospfInterfaceList[pid].IPv4 = nullptr;
        else if (af == AddressFamily::IPv6)
            iface.ospfInterfaceList[pid].IPv6 = nullptr;
        if (!iface.ospfInterfaceList[pid].IPv4 && !iface.ospfInterfaceList[pid].IPv6)
            iface.ospfInterfaceList.erase(pid);
    }

    delete dispatcher;
}

void OspfInterface::calculateCost()
{
    uint16_t oldCost = cost;
    uint16_t newCost{0};

    auto& configuredCost = configs->get<Config::OspfInterface::COST>();
    if (configuredCost.hasValue())
    {
        newCost = configuredCost.load();
    }
    else
    {
        uint32_t referenceBw = process.getConfigs().get<Config::Ospf::REFERENCE_BANDWIDTH>().load();
        uint32_t interfaceBw = iface.configs.bandwidth.load(std::memory_order_relaxed);
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
    auto* nbr = ntable.lookup(candDr);
    if (!nbr) return false;

    dr.rid.store(candDr, std::memory_order_release);
    dr.ip.store(nbr->ipAddress.raw, std::memory_order_release);
    return true;
}

bool OspfInterface::setBdr(uint32_t candBdr)
{
    auto* nbr = ntable.lookup(candBdr);
    if (!nbr) return false;

    bdr.rid.store(candBdr, std::memory_order_release);
    bdr.ip.store(nbr->ipAddress.raw, std::memory_order_release);
    return true;
}

void OspfInterface::election()
{
    // RFC 2328 §9.4 — two-pass DR/BDR election
    struct Candidate { uint32_t rid; uint8_t priority; uint32_t claimedDr; uint32_t claimedBdr; };

    uint32_t selfRid  = getArea().process().getRouterId();
    uint8_t  selfPrio = configs->get<Config::OspfInterface::PRIORITY>().load();

    // Build candidate list: self + all >= 2-way neighbors with priority > 0
    std::vector<Candidate> eligible;

    if (selfPrio > 0)
    {
        eligible.push_back({
            selfRid, selfPrio,
            dr.rid.load(std::memory_order_relaxed),
            bdr.rid.load(std::memory_order_relaxed)
        });
    }

    for (const auto& [rid, nbr] : ntable.neighbors)
    {
        if (nbr.getState() < Neighbor::State::TWOWAY) continue;
        uint8_t prio = nbr.priority.load(std::memory_order_relaxed);
        if (prio == 0) continue;
        eligible.push_back({
            rid, prio,
            nbr.dr.load(std::memory_order_relaxed),
            nbr.bdr.load(std::memory_order_relaxed)
        });
    }

    if (eligible.empty()) return;

    // Higher priority wins; tie-break by higher RID
    auto best = [](const Candidate& a, const Candidate& b) -> bool {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.rid > b.rid;
    };

    auto runElection = [&](uint32_t prevDr, uint32_t prevBdr) -> std::pair<uint32_t,uint32_t>
    {
        // Step 1: Elect BDR
        // Among eligible NOT declaring themselves DR, find highest prio/RID that claims BDR.
        // If none claim BDR, take the highest prio/RID not claiming DR.
        const Candidate* newBdrCand = nullptr;
        const Candidate* fallbackBdrCand = nullptr;

        for (const auto& c : eligible)
        {
            bool selfIsDr = (c.claimedDr == c.rid);
            if (selfIsDr) continue;  // Can't be BDR if claiming DR

            bool selfIsBdr = (c.claimedBdr == c.rid);

            if (selfIsBdr)
            {
                if (!newBdrCand || best(c, *newBdrCand))
                    newBdrCand = &c;
            }
            if (!fallbackBdrCand || best(c, *fallbackBdrCand))
                fallbackBdrCand = &c;
        }

        uint32_t newBdr = newBdrCand ? newBdrCand->rid
                        : (fallbackBdrCand ? fallbackBdrCand->rid : 0);

        // Step 2: Elect DR
        // Among eligible declaring themselves DR, take highest prio/RID.
        // If none, DR = BDR.
        const Candidate* newDrCand = nullptr;
        for (const auto& c : eligible)
        {
            if (c.claimedDr == c.rid)
            {
                if (!newDrCand || best(c, *newDrCand))
                    newDrCand = &c;
            }
        }

        uint32_t newDr = newDrCand ? newDrCand->rid : newBdr;

        return {newDr, newBdr};
    };

    uint32_t prevDr  = dr.rid.load(std::memory_order_relaxed);
    uint32_t prevBdr = bdr.rid.load(std::memory_order_relaxed);

    // First pass
    auto [newDr, newBdr] = runElection(prevDr, prevBdr);

    // Update self's claims to reflect election result, then run a second pass
    // so other candidates' views of us are updated (RFC 2328 §9.4 step 4)
    bool selfIsDr  = (newDr  == selfRid);
    bool selfIsBdr = (newBdr == selfRid);

    for (auto& c : eligible)
    {
        if (c.rid == selfRid)
        {
            c.claimedDr  = selfIsDr  ? selfRid : 0;
            c.claimedBdr = selfIsBdr ? selfRid : 0;
            break;
        }
    }

    // Second pass to stabilize
    auto [finalDr, finalBdr] = runElection(newDr, newBdr);

    bool drChanged  = (finalDr  != prevDr);
    bool bdrChanged = (finalBdr != prevBdr);

    setDr(finalDr);
    setBdr(finalBdr);

    isDr.store(finalDr  == selfRid, std::memory_order_release);
    isBdr.store(finalBdr == selfRid, std::memory_order_release);

    // If DR/BDR changed, trigger neighbor transitions and router LSA rebuild
    if (drChanged || bdrChanged)
    {
        // Neighbors that were TWOWAY and are now DR or BDR eligible need EXSTART
        for (auto& [rid, nbr] : ntable.neighbors)
        {
            if (nbr.getState() == Neighbor::State::TWOWAY)
                nbr.setState(Neighbor::State::EXSTART);
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
    auto& helloTimer = configs->get<Config::OspfInterface::HELLO_INTERVAL>();
    auto& helloMultiplier = configs->get<Config::OspfInterface::HELLO_MULTIPLIER>();
    auto& deadTimer = configs->get<Config::OspfInterface::DEAD_INTERVAL>();

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
            auto net = configs->get<Config::OspfInterface::NETWORK>().load();
            if (net == NetworkType::NON_BROADCAST || net == NetworkType::POINT_TO_MULTIPOINT_BROADCAST || net == NetworkType::POINT_TO_MULTIPOINT)
                ht = OSPF_MU_HELLO_TIME;
            else
                ht = OSPF_HELLO_TIME;
        }

        if (deadTimer.hasValue())
            dt = deadTimer.load();
        else
            dt = ht * 4;

        helloTime = std::chrono::seconds(ht);
        deadTime = std::chrono::seconds(dt);
    }
}

void OspfInterface::syncNetworkType()
{
    auto ntype = getConfigs().get<Config::OspfInterface::NETWORK>().load();

    syncTimers();
    isMulticast.store(
        ntype == NetworkType::BROADCAST ||
        ntype == NetworkType::POINT_TO_MULTIPOINT_BROADCAST ||
        ntype == NetworkType::POINT_TO_POINT,
        std::memory_order_release
    );
    getNTable().syncUnicast();
}

void OspfInterface::syncDigestKey()
{
    baseConfigs->get<Config::OspfInterfaceBase::MESSAGE_DIGEST_KEYS>().withRead([this](const std::vector<std::tuple<uint8_t, std::array<uint8_t, 16>, uint64_t>>& keys)
    {
        auto it = std::max_element(keys.begin(), keys.end(), [](const auto& a, const auto& b) {
            return std::get<2>(a) < std::get<2>(b);
        });

        if (it != keys.end())
        {
            authKey = readU128(std::get<1>(*it).data());
            authKeyId = std::get<0>(*it);
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
    configs->get<Config::OspfInterface::PASSIVE>().load();
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
