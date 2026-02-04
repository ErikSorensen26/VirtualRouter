// OspfInterface.cpp

#include "OspfInterface.h"
#include <OspfProcess.h>
#include <Interface.h>
#include <OspfNeighbor.h>
#include <OspfFlagManager.h>
#include <VirtualRouter.h>

#include <PacketDispatcher.h>
#include <PacketDispatcherV2.h>
#include <PacketDispatcherV3.h>

auto getIfaceAddr(Interface& iface, AddressFamily af) -> IPPrefix
{
    return af == AddressFamily::IPv4
        ? iface.configs.ipv4.getPrimaryPrefix()
        : iface.configs.ipv6.getLocalPrefix();
}

namespace OSPF
{
OspfInterface::OspfInterface(OspfProcess& proc, Interface& iface, Config::Reference<Config::OspfInterfaceBaseRegistry>& configs, OspfInterfaceId& id)
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
      tmgr(proc.tmgr, *this),
      iface(iface),
    configs([]() -> Config::Reference<Config::OspfInterfaceRegistry> {
        // TODO
    }()),
    baseConfigs([]() -> Config::Reference<Config::OspfInterfaceBaseRegistry> {
        // TODO
    }())
{
    syncConfigs();
    calculateCost();
    tmgr.startHello();
}

OspfInterface::~OspfInterface()
{
    uint32_t pid = process.getProcId();
    AddressFamily af = process.getAF();

    // TODO: expire originated in lsdb

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
    uint16_t oldCost = cost.load(std::memory_order_relaxed);
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
    
    cost.store(newCost, std::memory_order_release);

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
    dr.ip.store(readU128(nbr->ipAddress.raw), std::memory_order_release);
}

bool OspfInterface::setBdr(uint32_t candBdr)
{
    auto* nbr = ntable.lookup(candBdr);
    if (!nbr) return false;

    bdr.rid.store(candBdr, std::memory_order_release);
    bdr.ip.store(readU128(nbr->ipAddress.raw), std::memory_order_release);
}

void OspfInterface::election()
{
    // TODO: double check this is correct
    using Canidate = std::pair<uint32_t, uint8_t>;

    std::vector<Canidate> eligible;
    std::vector<Canidate> drClaims;
    std::vector<Canidate> bdrClaims;

    for (const auto& [rid, nbr] : ntable.neighbors)
    {
        if (nbr.getState() < Neighbor::State::TWOWAY)
            continue;

        uint prio = nbr.priority.load(std::memory_order_relaxed);
        if (prio == 0)
            continue;

        eligible.emplace_back(rid, prio);

        uint32_t claimedDr = nbr.dr.load(std::memory_order_relaxed);
        uint32_t claimedBdr = nbr.bdr.load(std::memory_order_relaxed);

        if (claimedDr != 0 && claimedBdr == rid)
            drClaims.emplace_back(rid, prio);

        if (claimedBdr != 0 && claimedBdr == rid)
            bdrClaims.emplace_back(rid, prio);
    }

    // Add self
    uint32_t selfRid = getArea().process().getRouterId();
    uint8_t selfPrio = configs->get<Config::OspfInterface::PRIORITY>().load();
    
    if (selfPrio > 0)
        eligible.emplace_back(selfRid, selfPrio);

    if (eligible.size() < 2)
        return;

    auto sortList = [](std::vector<Canidate>& v)
    {
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second)
                return a.second > b.second;
            return a.first > b.first;
        });
    };

    sortList(eligible);
    sortList(drClaims);
    sortList(bdrClaims);

    uint32_t newDr = 0;
    uint32_t newBdr = 0;

    // Elect bdr
    for (const auto& c : bdrClaims)
    {
        bool claimsDr = std::any_of(
            drClaims.begin(), drClaims.end(),
            [&](const Canidate& d) { return d.first == c.first; });

        if (!claimsDr)
        {
            newBdr = c.first;
            break;
        }
    }

    if (newBdr == 0)
        newBdr = eligible.front().first;

    // Elect dr
    if (!drClaims.empty())
    {
        newDr = drClaims.front().first;
    }
    else
    {
        newDr = newBdr;
        
        for (const auto& c : eligible)
        {
            if (c.first != newDr)
            {
                newBdr = c.first;
                break;
            }
        }
    }

    setDr(newDr);
    setBdr(newBdr);
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
        helloTime.store(std::chrono::seconds(1) / helloMultiplier.load(), std::memory_order_release);
        deadTime.store(std::chrono::seconds(1), std::memory_order_release);
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

        helloTime.store(std::chrono::seconds(ht), std::memory_order_release);
        deadTime.store(std::chrono::seconds(dt), std::memory_order_release);
    }
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

OspfArea& OspfInterface::getArea()
{
    return area;
}
}
