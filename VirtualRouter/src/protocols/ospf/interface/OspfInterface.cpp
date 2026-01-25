// OspfInterface.cpp

#include "OspfInterface.h"
#include <OspfProcess.h>
#include <Interface.h>
#include <OspfNeighbor.h>
#include <OspfTopology.h>
#include <OspfFlagManager.h>
#include <VirtualRouter.h>

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
    : process(proc),
      topology(&proc.insureTopology(iface.configs.tid.load(std::memory_order_relaxed))),
      area(&(topology.load()->insureArea(id.area))),
      id(id),
      interfaceId(iface.configs.key),
      interfaceAddress(getIfaceAddr(iface, process.getAF())),
      dispatcher(proc.isV3 ? new PacketDispatcherV3(*this, configs) : PacketDispatcherV2(*this, configs)),
      flags(*this),
      lsaFlags(*this),
      ntable(*this),
      tmgr(proc.tmgr, *this),
      iface(iface)
{
    syncConfigs();
    tmgr.startHello();
}

OspfInterface::~OspfInterface()
{
    uint32_t pid = process.getProcId();
    AddressFamily af = process.getAF();

    // TODO: clear connected

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

    {
        std::shared_lock<std::shared_mutex> lock(ntable.mu);

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
    }

    // Add self
    uint32_t selfRid = getArea().topology().process.getRouterId();
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
}

void OspfInterface::setPassiveMode(bool passive)
{
    configs->get<Config::OspfInterface::PASSIVE>().load();
    if (passive)
    {
        std::shared_lock<std::shared_mutex> lock(ntable.mu);
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
    return *area.load(std::memory_order_relaxed);
}
}
