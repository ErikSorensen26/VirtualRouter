// Global.cpp

#include <string>
#include <unordered_map>
#include <mutex>

#include "Global.h"
#include "configs/registry/global/GlobalRegistry.h"
#include "configs/registry/global/VrfRegistry.h"
#include "configs/registry/interface/InterfaceRegistry.h"
#include "VirtualRouter.h"
#include "interface/Interface.h"
#include "hardware/HardwareManager.h"
#include "configs/FieldAccessor.hpp"
#include "bgp/BgpProcess.h"

namespace core
{

config::GlobalRegistry& Global::getConfigs()
{
    return *configs;
}

Global::Global(cli::FileSystem& fs, const cli::StartupFiles& stfs, bool enableRouting, bool test)
    : threadPool(/*std::thread::hardware_concurrency()*/5),
      timeManager(threadPool),
      scheduler(threadPool, timeManager),
      configs(new config::GlobalRegistry()),
      routingEnabled(enableRouting),
      engine(*this, stfs, fs, test)
{
    initConfigs();
    configs->context().set(this);
    configs->get<config::Global::HOSTNAME>().set(DEFAULT_HOSTNAME);

    txMgr.setCorePool({0, 1, 2, 3});
    txMgr.setCpuPolicy(qos::egress::CpuPolicy::EqualShare);
    txMgr.setTxCoreBias(1.0);

    rxMgr.setCorePool({4, 5, 6, 7});
    rxMgr.setCpuPolicy(qos::ingress::RxQueueManager::CpuPolicy::EqualShare);

    configs->get<config::Global::VRF_CONFIGS>().emplaceBack(DEFAULT_VRF);
}

Global::~Global()
{
    if (dhcpServer)
        delete dhcpServer;
    if (dhcpv6Server)
        delete dhcpv6Server;
    if (bgpProcess)
        delete bgpProcess;
    routingInstances.clear();
}

void Global::initConfigs()
{
    if (configs) return;
    // TODO apply configs here
    configs = new config::GlobalRegistry();
}

void Global::setHostname(const std::string& name)
{
    getConfigs().get<config::Global::HOSTNAME>().set(name);
}

std::string Global::getHostname()
{
    return getConfigs().get<config::Global::HOSTNAME>().load();
}

void Global::reset()
{
    if (configs) delete configs;
    initConfigs();
    setHostname(DEFAULT_HOSTNAME);
    setIPv6UnicastRouting(false);
    setAAA(false);
    configs->get<config::Global::VRF_CONFIGS>().emplaceBack(DEFAULT_VRF);
}
      
// Interfaces

interface::Interface* Global::addInterface(interface::InterfaceKey key, config::InterfaceRegistry& cfg)
{
    const hardware::HwIfaceInfo* info = engine.hwManager.getHwInfo(key);
    if (!info)
        return nullptr;
    auto [type, id] = key.decode();
    interface::InterfaceCreation iface = {type, id, *getRoutingInstance(DEFAULT_VRF), *info, cfg};
    auto [it, ok] = interfaceList.try_emplace(key, iface);
    return ok ? &it->second : nullptr;
}

interface::Interface* Global::getInterface(interface::InterfaceKey key)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (interfaceList.find(key) != interfaceList.end())
        return &interfaceList.at(key);
    return nullptr;
}

bool Global::removeInterface(interface::InterfaceKey key)
{
    return interfaceList.erase(key) != 0;
}

VirtualRouter* Global::addRoutingInstance(const std::string& name, config::VrfRegistry& cfg)
{
    auto [it, ok] = routingInstances.try_emplace(name, *this, name, cfg);
    return ok ? &it->second : nullptr;
}

VirtualRouter* Global::getRoutingInstance(const std::string& name, types::AddressFamily ad)
{
    std::lock_guard<std::mutex> lock(routingInstanceMutex);
    if (routingInstances.find(name) != routingInstances.end() && 
        ad != types::AddressFamily::NONE 
        ? routingInstances.at(name).enabledAddressFamilies.count(ad)
        : true)
    {
        return &routingInstances.at(name);
    }
    return nullptr;
}

bool Global::removeRoutingInstance(const std::string& name)
{
    if (name == DEFAULT_VRF) return false;
    return routingInstances.erase(name) != 0;
}

routing::bgp::BgpProcess* Global::addBgp(config::BgpRegistry& reg, uint32_t as)
{
    if (auto bgp = bgpProcess; bgp)
        return nullptr;
    bgpProcess = new routing::bgp::BgpProcess(reg, as, *this);
    return bgpProcess;
}

routing::bgp::BgpProcess* Global::getBgp()
{
    return bgpProcess;
}

services::dhcp::DhcpServer* Global::getDhcpServer()
{
    return dhcpServer;
}

services::dhcp::Dhcpv6Server* Global::getDhcpv6Server()
{
    return dhcpv6Server;
}

bool Global::removeBgp(uint32_t as)
{
    if (bgpProcess && bgpProcess->asNumber == as)
    {
        delete bgpProcess;
        return true;
    }
    return false;
}
} // namespace core
