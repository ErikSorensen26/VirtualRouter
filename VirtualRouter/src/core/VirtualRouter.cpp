// VirtualRouter.cpp

#include "VirtualRouter.h"
#include "Global.h"
#include "configs/registry/global/GlobalRegistry.h"
#include "interface/Interface.h"
#include "interface/configs/InterfaceType.hpp"
#include "eigrp/core/Eigrp.h"
#include "ospf/OspfProcess.h"

namespace core
{

VirtualRouter::VirtualRouter(Global& g, const std::string& name, config::VrfRegistry& cfg)
    : defaulted(name == DEFAULT_VRF),
      configs(cfg),
      tcpManager(*this),
      routingTable(g.scheduler),
      global(g)
{
    instanceName = name;
    enabledAddressFamilies.insert(types::AddressFamily::IPv4);
    configs.context().set(this);

    // TODO initiate routing protocols
}

// Destructor
VirtualRouter::~VirtualRouter()
{
    //assert(empty());
    // Eigrp Autonomous Systems
    for (auto& it : eigrpList)
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
}

bool VirtualRouter::empty()
{
    return ifaceMgr.empty() &&
           eigrpList.empty() &&
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
routing::eigrp::Eigrp& VirtualRouter::addEigrpAutonomousSystem(config::EigrpRegistry& reg, uint16_t id, types::AddressFamily af)
{
    if (af != types::AddressFamily::IPv4 && af != types::AddressFamily::IPv6)
        throw std::runtime_error("Eigrp only supports address families ipv4 and ipv6");
    routing::eigrp::EigrpAutonomousSystem& as = eigrpList[id];
    if (af == types::AddressFamily::IPv4)
    {
        if (!as.ipv4) as.ipv4 = new routing::eigrp::Eigrp(reg, id, af, this);
        return *as.ipv4;
    }
    else
    {
        if (!as.ipv6) as.ipv6 = new routing::eigrp::Eigrp(reg, id, af, this);
        return *as.ipv6;
    }
}

routing::eigrp::Eigrp* VirtualRouter::getEigrpAutonomousSystem(uint16_t id, types::AddressFamily af)
{
    if (auto it = eigrpList.find(id); it != eigrpList.end())
    {
        if (af == types::AddressFamily::IPv4)
        {
            if (it->second.ipv4)
                return it->second.ipv4;
        }
        else if (af == types::AddressFamily::IPv6)
        {
            if (it->second.ipv6)
                return it->second.ipv6;
        }
    }
    return nullptr;
}

bool VirtualRouter::removeEigrpAutonomousSystem(uint16_t id, types::AddressFamily af)
{
    if (auto it = eigrpList.find(id); it != eigrpList.end())
    {
        if (af == types::AddressFamily::IPv4)
        {
            if (it->second.ipv4)
            {
                delete it->second.ipv4;
                it->second.ipv4 = nullptr;
            }
            if (!it->second.ipv4 && !it->second.ipv6)
                eigrpList.erase(id);
            return true;
        }
        else if (af == types::AddressFamily::IPv6)
        {
            if (it->second.ipv6)
            {
                delete it->second.ipv6;
                it->second.ipv6 = nullptr;
            }
            if (!it->second.ipv4 && !it->second.ipv6)
                eigrpList.erase(id);
            return true;
        }
    }
    return false;
}

routing::ospf::OspfProcess& VirtualRouter::addOspf(config::OspfRegistry& reg, uint16_t id)
{
    if (auto it = ospfList.find(id); it == ospfList.end())
        ospfList.try_emplace(id, reg, false, id, types::AddressFamily::IPv4, this);
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

routing::ospf::OspfProcess& VirtualRouter::addOspfv3(config::OspfRegistry& reg, uint16_t id, types::AddressFamily af)
{
    if (af != types::AddressFamily::IPv4 && af != types::AddressFamily::IPv6)
        throw std::runtime_error("Eigrp only supports address families ipv4 and ipv6");
    routing::ospf::Ospfv3Instance& proc = ospfv3List[id];
    if (af == types::AddressFamily::IPv4)
    {
        if (!proc.ipv4) proc.ipv4 = new routing::ospf::OspfProcess(reg, true, id, af, this);
        return *proc.ipv4;
    }
    else
    {
        if (!proc.ipv6) proc.ipv6 = new routing::ospf::OspfProcess(reg, true, id, af, this);
        return *proc.ipv6;
    }
}

routing::ospf::OspfProcess* VirtualRouter::getOspfv3(uint16_t id, types::AddressFamily af)
{
    if (auto it = ospfv3List.find(id); it != ospfv3List.end())
    {
        if (af == types::AddressFamily::IPv4)
        {
            if (it->second.ipv4)
                return it->second.ipv4;
        }
        else if (af == types::AddressFamily::IPv6)
        {
            if (it->second.ipv6)
                return it->second.ipv6;
        }
    }
    return nullptr;
}

bool VirtualRouter::removeOspfv3(uint16_t id, types::AddressFamily af)
{
    if (auto it = ospfv3List.find(id); it != ospfv3List.end())
    {
        if (af == types::AddressFamily::IPv4)
        {
            if (it->second.ipv4)
            {
                delete it->second.ipv4;
                it->second.ipv4 = nullptr;
            }
            if (!it->second.ipv4 && !it->second.ipv6)
                ospfv3List.erase(id);
            return true;
        }
        else if (af == types::AddressFamily::IPv6)
        {
            if (it->second.ipv6)
            {
                delete it->second.ipv6;
                it->second.ipv6 = nullptr;
            }
            if (!it->second.ipv4 && !it->second.ipv6)
                ospfv3List.erase(id);
            return true;
        }
    }
    return false;
}

config::VrfRegistry& VirtualRouter::getConfigs()
{
    return configs;
}

config::GlobalRegistry& VirtualRouter::getGlobalConfigs()
{
    return global.getConfigs();
}


ControlScheduler& VirtualRouter::getControlScheduler()
{
    return global.scheduler;
}

} // namespace core
