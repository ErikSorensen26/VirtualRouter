// Global.cpp

#include <string>
#include <map>
#include <mutex>

#include "Global.h"
#include "VirtualRouter.h"
#include "interface/Interface.h"
#include "hardware/HardwareManager.h"

namespace core
{

Global::Global(cli::FileSystem& fs, const cli::StartupFiles& stfs, bool enableRouting, bool test)
    : routingEnabled(enableRouting),
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

    if (!test) {
        engine.initEngine(stfs);
    }
}

Global::~Global()
{
    if (dhcpServer)
        delete dhcpServer;
    if (dhcpv6Server)
        delete dhcpv6Server;

    {
        std::lock_guard<std::mutex> lock(routingInstanceMutex);
        for (auto& [_, instance] : routingInstances)
            delete instance;
    }
    {
        std::lock_guard<std::mutex> lock(interfaceMutex);
        for (auto& [_, interface] : interfaceList)
            delete interface;
        interfaceList.clear();
    }
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

    {
        std::lock_guard<std::mutex> lock(routingInstanceMutex);
        for (auto& [_, instance] : routingInstances)
            delete instance;
        routingInstances.clear();
    }
    {
        std::lock_guard<std::mutex> lock(interfaceMutex);
        for (auto& [_, interface] : interfaceList)
            delete interface;
        interfaceList.clear();
    }
    
    addRoutingInstance("default");
}
      
// Interfaces
interface::Interface* Global::addInterface(interface::InterfaceType interfaceType, const hardware::HwIfaceInfo& hwInfo, float interfaceId, bool debug)
{
    uint32_t key = calculateInterfaceKey(interfaceType, interfaceId);
    if (interfaceList.find(key) != interfaceList.end())
    {
        return nullptr;
    }
    interface::InterfaceCreation iface = {interfaceType, interfaceId, *getRoutingInstance("default"), hwInfo, debug};
    interfaceList[key] = new interface::Interface(iface);

    return interfaceList[key];
}

interface::Interface* Global::getInterface(uint32_t key)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (interfaceList.find(key) != interfaceList.end())
    {
        return interfaceList[key];
    }
    return nullptr;
}

std::map<uint32_t, interface::Interface*>& Global::getInterfaceList()
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    return interfaceList;
}

bool Global::removeInterface(uint32_t key)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (auto it = interfaceList.find(key); it != interfaceList.end())
    {
        std::string hwIface = it->second->configs.hwInfo.ifname;
        delete interfaceList[key];
        interfaceList.erase(key);
        return true;
    }
    return false;
}

VirtualRouter* Global::addRoutingInstance(const std::string& name)
{
    std::lock_guard<std::mutex> lock(routingInstanceMutex);
    if (routingInstances.find(name) != routingInstances.end())
    {
        return nullptr;
    }
    routingInstances[name] = new VirtualRouter(*this, name);
    return routingInstances[name];
}

VirtualRouter* Global::getRoutingInstance(const std::string& name, types::AddressFamily ad)
{
    std::lock_guard<std::mutex> lock(routingInstanceMutex);
    if (routingInstances.find(name) != routingInstances.end() && 
        ad != types::AddressFamily::NONE 
        ? routingInstances[name]->enabledAddressFamilies.count(ad)
        : true)
    {
        return routingInstances[name];
    }
    return nullptr;
}

bool Global::removeRoutingInstance(const std::string& name)
{
    if (name == "default") return false; // Can't delete the default instance
    std::lock_guard<std::mutex> lock(routingInstanceMutex);
    if (routingInstances.find(name) != routingInstances.end())
    {
        delete routingInstances[name];
        routingInstances[name] = nullptr;
        routingInstances.erase(name);
        return true;
    }
    return false;
}

} // namespace core
