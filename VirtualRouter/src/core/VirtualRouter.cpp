// VirtualRouter.cpp

#include "VirtualRouter.h"
#include "Global.h"
#include "interface/Interface.h"
#include "interface/configs/InterfaceType.hpp"
#include "eigrp/core/Eigrp.h"
#include "ospf/OspfProcess.h"

namespace core
{

VirtualRouter::VirtualRouter(Global& g, const std::string& name)
    : defaulted(name == "default"),
      configs([&g, &name]() -> config::VrfRegistry& {
          auto& vrfs = g.configs.reg.get<config::Global::VRF_CONFIGS>();
          return vrfs.emplaceBack(name);
      }()),
      tcpManager(*this),
      routingTable(g.scheduler),
      global(g)
{
    instanceName = name;
    enabledAddressFamilies.insert(types::AddressFamily::IPv4);

    // TODO initiate routing protocols
}

// Destructor
VirtualRouter::~VirtualRouter()
{
    assert(empty());
    // Eigrp Autonomous Systems
    for (auto it : eigrpList)
    {
        if (it.second.ipv4)
        {
            delete it.second.ipv4;
        }
        if (it.second.ipv6)
        {
            delete it.second.ipv6;
        }
    }
    eigrpList.clear();

    namedEigrpList.clear();
}

bool VirtualRouter::empty()
{
    return ifaceMgr.empty() &&
           eigrpList.empty() &&
           namedEigrpList.empty() &&
           ospfList.empty() &&
           ospfv3List.empty();
}

bool VirtualRouter::calculateRID(uint32_t& rid)
{
    uint32_t highestIP = 0;
    uint32_t tempIp;

    auto processID = [&](interface::Interface* interface)
    {
        if (interface->shutdownFlag.load(std::memory_order_relaxed)) return;
        auto& interfaceInfo = interface->configs;
        tempIp = interfaceInfo.ipv4.getPrimaryAddress().addr;
        if (tempIp == 0) return;
        if (tempIp < highestIP) return;
        highestIP = tempIp;
    };

    auto interfaceList = ifaceMgr.snapshot();
    
    for (const auto& [id, interface] : interfaceList)
    {
        if (interface->configs.interfaceType != interface::InterfaceType::LOOPBACK) continue;
        processID(interface);
    }
    if (highestIP == 0)
    {
        for (const auto& [id, interface] : interfaceList)
        {
            processID(interface);
        }
    }
    rid = highestIP;
    return highestIP != 0;
}

// Eigrp Autonomous Systems
routing::eigrp::EigrpAutonomousSystem* VirtualRouter::addEigrpAutonomousSystem(uint16_t id)
{
    if (eigrpList.contains(id))
        return nullptr;
    return &eigrpList[id];
}

routing::eigrp::EigrpAutonomousSystem* VirtualRouter::getEigrpAutonomousSystem(uint16_t id)
{
    if (auto it = eigrpList.find(id); it != eigrpList.end())
        return &it->second;
    return nullptr;
}

bool VirtualRouter::removeEigrpAutonomousSystem(uint16_t id)
{
    if (eigrpList.find(id) != eigrpList.end())
    {
        eigrpList.erase(id);
        return true;
    }
    return false;
}

// Eigrp Named Systems
routing::eigrp::EigrpNamed& VirtualRouter::addEigrpNamed(const std::string& name)
{
    return namedEigrpList[name];
}

routing::eigrp::EigrpNamed* VirtualRouter::getEigrpNamed(const std::string& name)
{
    if (auto it = namedEigrpList.find(name); it != namedEigrpList.end())
        return &it->second;
    return nullptr;
}

bool VirtualRouter::removeEigrpNamed(const std::string& name)
{
    if (namedEigrpList.find(name) != namedEigrpList.end())
    {
        auto& eigrp = namedEigrpList[name];
        for (auto& [subName, pair] : eigrp.systems)
        {
            auto [v4, v6] = pair;
            if (v4)
            {
                uint32_t as = v4->getAS();
                if (eigrpList.find(as) != eigrpList.end())
                {
                    delete eigrpList[as].ipv4;
                    eigrpList[as].ipv4 = nullptr;
                    if (!eigrpList[as].ipv6)
                        removeEigrpAutonomousSystem(as);
                }
            }
            if (v6)
            {
                uint32_t as = v6->getAS();
                if (eigrpList.find(as) != eigrpList.end())
                {
                    delete eigrpList[as].ipv6;
                    eigrpList[as].ipv6 = nullptr;
                    if (!eigrpList[as].ipv4)
                        removeEigrpAutonomousSystem(as);
                }
            }
        }
        namedEigrpList.erase(name);
        return true;
    }
    return false;
}

routing::ospf::OspfProcess& VirtualRouter::addOspf(uint16_t id)
{
    if (auto it = ospfList.find(id); it == ospfList.end())
    {
        ospfList.try_emplace(id, false, id, types::AddressFamily::IPv4, this);
        return it->second;
    }
    return ospfList.at(id);
}

routing::ospf::OspfProcess* VirtualRouter::getOspf(uint16_t id)
{
    if (auto it = ospfList.find(id); it != ospfList.end())
        return &it->second;
    return nullptr;
}

bool VirtualRouter::removeOspf(uint16_t id)
{
    if (auto it = ospfList.find(id); it != ospfList.end()) 
    {
        ospfList.erase(it);
        return true;
    }
    return false;
}

routing::ospf::OspfV3Instance& VirtualRouter::addOspfv3(uint16_t id)
{
    return ospfv3List.at(id);
}

routing::ospf::OspfProcess& VirtualRouter::addOspfv3(uint16_t id, types::AddressFamily af)
{
    if (ospfv3List.find(id) == ospfv3List.end())
    {
        config::OspfAddressFamilyV3Registry& afConfigs = global.registry.create<config::OspfAddressFamilyV3Registry>();
        ospfv3List.emplace(id, afConfigs);
    }
    auto ospf = ospfv3List.at(id);

    if (af == types::AddressFamily::IPv4)
    {
        if (!ospf.ipv4)
            ospf.ipv4 = new routing::ospf::OspfProcess(true, id, af, this);
        return *ospf.ipv4;
    }
    else
    {
        if (!ospf.ipv6)
            ospf.ipv6 = new routing::ospf::OspfProcess(true, id, af, this);
        return *ospf.ipv6;
    }
}

routing::ospf::OspfV3Instance* VirtualRouter::getOspfv3(uint16_t id)
{
    if (auto it = ospfv3List.find(id); it != ospfv3List.end())
        return &it->second;
    return nullptr;
}

bool VirtualRouter::removeOspfv3(uint16_t id)
{
    if (auto it = ospfv3List.find(id); it != ospfv3List.end())
    {
        auto& ospf = it->second;
        if (ospf.ipv4)
        {
            delete ospf.ipv4;
            ospf.ipv4 = nullptr;
        }
        if (ospf.ipv6)
        {
            delete ospf.ipv6;
            ospf.ipv6 = nullptr;
        }
        ospfv3List.erase(id);
        return true;
    }
    return false;
}

bool VirtualRouter::removeOspfv3(uint16_t id, types::AddressFamily af)
{
    if (auto it = ospfv3List.find(id); it != ospfv3List.end())
    {
        auto& ospf = it->second;
        if (af == types::AddressFamily::IPv4)
        {
            if (ospf.ipv4)
            {
                delete ospf.ipv4;
                ospf.ipv4 = nullptr;
            }
        }
        else
        {
            if (ospf.ipv6)
            {
                delete ospf.ipv6;
                ospf.ipv6 = nullptr;
            }
        }

        if (!ospf.ipv4 && !ospf.ipv6)
            ospfv3List.erase(id);
        return true;
    }
    return false;
}

void VirtualRouter::refreshEigrpV4()
{
    for (auto& [id, as] : eigrpList)
        if (as.ipv4) as.ipv4->getIfaceMgr().refreshInterfaceList();
    for (auto& [name, named] : namedEigrpList)
        for (auto& [subName, pair] : named.systems)
            if (pair.first) pair.first->getIfaceMgr().refreshInterfaceList();
}

void VirtualRouter::refreshEigrpV6()
{
    for (auto& [id, as] : eigrpList)
        if (as.ipv6) as.ipv6->getIfaceMgr().refreshInterfaceList();
    for (auto& [name, named] : namedEigrpList)
        for (auto& [subName, pair] : named.systems)
            if (pair.second) pair.second->getIfaceMgr().refreshInterfaceList();
}

void VirtualRouter::refreshEigrpV6Interfaces()
{
    for (auto& [id, as] : eigrpList)
        if (as.ipv6) as.ipv6->getIfaceMgr().refreshInterfaceList();
    for (auto& [name, named] : namedEigrpList)
        for (auto& [subName, pair] : named.systems)
            if (pair.second) pair.second->getIfaceMgr().refreshInterfaceList();
}

config::VrfRegistry& VirtualRouter::getConfigs()
{
    return configs;
}

config::GlobalRegistry& VirtualRouter::getGlobalConfigs()
{
    return global.configs;
}

config::Registry& VirtualRouter::getRegistry()
{
    return global.registry;
}

ControlScheduler& VirtualRouter::getControlScheduler()
{
    return global.scheduler;
}

} // namespace core
