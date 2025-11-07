#include <Global.h>
#include <Interface.h>
#include <InterfaceConfigs.h>
#include <RoutingTable.h>
#include <VirtualRouter.h>
#include <HardwareManager.h>

#include <string>
#include <map>
#include <mutex>

Global::Global(const StartupFiles& stfs, bool enableRouting, bool test)
    : routingEnabled(enableRouting), threadPool(/*std::thread::hardware_concurrency()*/5), timeManager(threadPool), engine(*this, stfs, test)
{
    txMgr.setCorePool({0, 1, 2, 3});
    txMgr.setCpuPolicy(CpuPolicy::EqualShare);
    txMgr.setTxCoreBias(1.0);
}

Global::Global(IFileSystem* fs, const StartupFiles& stfs, bool test) : threadPool(std::thread::hardware_concurrency()), timeManager(threadPool), engine(*this, stfs, fs, test) {}

Global::~Global()
{
    if (dhcpServer)
        delete dhcpServer;
    if (dhcpv6Server)
        delete dhcpv6Server;

    std::lock_guard<std::mutex> lock(routingInstanceMutex);
    for (auto& [_, instance] : routingInstances)
        delete instance;
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
    
    addRoutingInstance("default");
}
      
// Interfaces
Interface* Global::addInterface(InterfaceType interfaceType, const size_t inQueSiz, const size_t outQueSiz, const HwIfaceInfo& hwInfo, float interfaceId, bool debug)
{
    uint32_t key = calculateInterfaceKey(interfaceType, interfaceId);
    if (interfaceList.find(key) != interfaceList.end())
    {
        return nullptr;
    }
    engine.hwManager->bringUp(hwInfo.iface);
    InterfaceCreation iface = {interfaceType, interfaceId, *getRoutingInstance("default"), hwInfo, debug};
    interfaceList[key] = new Interface(iface);

    return interfaceList[key];
}

Interface* Global::getInterface(uint32_t key)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (interfaceList.find(key) != interfaceList.end())
    {
        return interfaceList[key];
    }
    return nullptr;
}

std::map<uint32_t, Interface*> Global::getInterfaceList()
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    return interfaceList;
}

bool Global::removeInterface(uint32_t key)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (auto it = interfaceList.find(key); it != interfaceList.end())
    {
        std::string hwIface = it->second->configs.physicalInterface;
        delete interfaceList[key];
        engine.hwManager->bringDown(hwIface);
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

VirtualRouter* Global::getRoutingInstance(const std::string& name, AddressFamily ad)
{
    std::lock_guard<std::mutex> lock(routingInstanceMutex);
    if (routingInstances.find(name) != routingInstances.end() && 
        ad != AddressFamily::NONE 
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
