// Global.cpp

#include <string>
#include <unordered_map>
#include <mutex>

#include "Global.h"
#include "VirtualRouter.h"
#include "interface/Interface.h"
#include "hardware/HardwareManager.h"

namespace core
{

Global::Global(cli::FileSystem& fs, const cli::StartupFiles& stfs, bool enableRouting, bool test)
    : routingEnabled(enableRouting),
      registry(),
      configs([&]() {
          // TODO load registry if needed
          return registry.create<config::GlobalRegistry>();
      }()),
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

    // Build required global registries
    registry.emplace(configs->get<config::Global::IPV6_ND>());

    // Load VRFs out of global configs
    {
        std::lock_guard<std::mutex> lock(routingInstanceMutex);
        for (const auto& [name, _] : configs->get<config::Global::VRF_CONFIGS>())
        {
            if (routingInstances.find(name) != routingInstances.end())
                continue;
            routingInstances.try_emplace(name, *this, name);
        }
    }

    // Load Interfaces out of global scope and assign correct VRFs
    {
        std::lock_guard<std::mutex> lock(interfaceMutex);
        for (const auto& [id, cfg] : configs->get<config::Global::INTERFACE>())
        {
            auto [type, key] = id.decode();
            uint32_t hwIface = engine.hwManager.getInterface(type, static_cast<int>(std::floor(key)));
            const hardware::HwIfaceInfo* info = engine.hwManager.getHwInfo(hwIface);
            if (!info) continue;

            if (interfaceList.find(id) != interfaceList.end())
                continue;

            auto& ifaceVrfField = cfg->get<config::Interface::VRF_FORWARDING>();
            std::string ifaceVrf;
            ifaceVrfField.withRead([&ifaceVrf](const std::string& v) { ifaceVrf = v; });

            interface::InterfaceCreation iface = {type, key, *getRoutingInstance(ifaceVrf), *info};
            interfaceList.emplace(id, iface);
        }
    }
}

Global::~Global()
{
    if (dhcpServer)
        delete dhcpServer;
    if (dhcpv6Server)
        delete dhcpv6Server;
}

void Global::setHostname(const std::string& name)
{
    std::unique_lock<std::shared_mutex> lock(hostnameMutex);
    hostname = name;
}

std::string Global::getHostname()
{
    std::shared_lock<std::shared_mutex> lock(hostnameMutex);
    return hostname;
}

void Global::reset()
{
    setHostname(DEFAULT_HOSTNAME);
    setIPv6UnicastRouting(false);
    setAAA(false);
    addRoutingInstance("default");
}
      
// Interfaces
interface::Interface* Global::addInterface(interface::InterfaceType interfaceType, const hardware::HwIfaceInfo& hwInfo, float interfaceId, bool debug)
{
    interface::InterfaceKey key(interfaceType, interfaceId);
    if (interfaceList.find(key) != interfaceList.end())
    {
        return nullptr;
    }
    interface::InterfaceCreation iface = {interfaceType, interfaceId, *getRoutingInstance("default"), hwInfo, debug};
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

std::unordered_map<interface::InterfaceKey, interface::Interface>& Global::getInterfaceList()
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    return interfaceList;
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
