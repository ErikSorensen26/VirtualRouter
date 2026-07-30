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

namespace core
{

config::GlobalRegistry& Global::getConfigs()
{
    return *pConfigs;
}

Global::Global(cli::FileSystem& fs, const cli::StartupFiles& stfs, bool enableRouting, bool test)
    : routingEnabled(enableRouting),
      pConfigs(std::make_unique<config::GlobalRegistry>()),
      configs(*pConfigs),
      threadPool(/*std::thread::hardware_concurrency()*/5),
      timeManager(threadPool),
      scheduler(threadPool, timeManager),
      engine(*this, stfs, fs, test)
{
    txMgr.setCorePool({0, 1, 2, 3});
    txMgr.setCpuPolicy(qos::egress::CpuPolicy::EqualShare);
    txMgr.setTxCoreBias(1.0);

    rxMgr.setCorePool({4, 5, 6, 7});
    rxMgr.setCpuPolicy(qos::ingress::RxQueueManager::CpuPolicy::EqualShare);

    setHostname(DEFAULT_HOSTNAME);

    // Load VRFs out of global configs
    routingInstanceRefresh();
    // Load Interfaces out of global scope and assign correct VRFs
    interfaceRefresh();
}

Global::~Global()
{
    if (dhcpServer)
        delete dhcpServer;
    if (dhcpv6Server)
        delete dhcpv6Server;
    routingInstances.clear();
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
    setHostname(DEFAULT_HOSTNAME);
    setIPv6UnicastRouting(false);
    setAAA(false);
    addRoutingInstance("default");
}
      
// Interfaces
void Global::interfaceRefresh()
{
    {
        std::lock_guard<std::mutex> lock(interfaceMutex);
        auto interfaceCfgs = getConfigs().get<config::Global::INTERFACE>();
        
        // Erase
        for (auto it = interfaceList.begin(); it != interfaceList.end();)
        {
            if (interfaceCfgs.find(it->first) == interfaceCfgs.end())
                it = interfaceList.erase(it);
            else
                ++it;
        }

        for (auto& [id, cfg] : interfaceCfgs)
        {
            if (!interfaceList.contains(id))
            {
                auto [type, key] = id.decode();
                const hardware::HwIfaceInfo* info = engine.hwManager.getHwInfo(id);
                if (!info) continue;

                std::string ifaceVrf = cfg->get<config::Interface::VRF_FORWARDING>().load();

                interface::InterfaceCreation iface = {type, key, *getRoutingInstance(ifaceVrf), *info};
                interfaceList.emplace(id, iface);
            }
        }
    }
}

interface::Interface* Global::addInterface(interface::InterfaceKey key, const hardware::HwIfaceInfo& hwInfo, bool debug)
{
    if (interfaceList.find(key) != interfaceList.end())
        return nullptr;
    auto [type, id] = key.decode();
    interface::InterfaceCreation iface = {type, id, *getRoutingInstance("default"), hwInfo, debug};
    interfaceList.emplace(key, iface);
    return &interfaceList.at(key);
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
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (auto it = interfaceList.find(key); it != interfaceList.end())
    {
        interfaceList.erase(key);
        return true;
    }
    return false;
}

void Global::routingInstanceRefresh()
{
    {
        std::lock_guard<std::mutex> lock(routingInstanceMutex);
        auto vrfConfigs = getConfigs().get<config::Global::VRF_CONFIGS>();

        // Erase
        for (auto it = routingInstances.begin(); it != routingInstances.end();)
        {
            if (vrfConfigs.find(it->first) == vrfConfigs.end())
                it = routingInstances.erase(it);
            else
                ++it;
        }

        for (const auto& [name, _] : vrfConfigs)
        {
            if (!routingInstances.contains(name))
                routingInstances.try_emplace(name, *this, name);
        }
    }
}

VirtualRouter* Global::addRoutingInstance(const std::string& name)
{
    std::lock_guard<std::mutex> lock(routingInstanceMutex);
    if (routingInstances.find(name) != routingInstances.end())
        return nullptr;
    routingInstances.try_emplace(name, *this, name);
    return &routingInstances.at(name);
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
    if (name == "default") return false; // Can't delete the default instance
    std::lock_guard<std::mutex> lock(routingInstanceMutex);
    if (routingInstances.find(name) != routingInstances.end())
    {
        routingInstances.erase(name);
        return true;
    }
    return false;
}

} // namespace core
