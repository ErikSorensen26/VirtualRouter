#include <Global.h>
#include <Interface.h>
#include <RoutingTable.h>
#include <VirtualRouter.h>
#include <InterfaceConfigs.h>

#include <string>
#include <map>
#include <mutex>

Global::Global(bool enableRouting, bool test) : routingEnabled(enableRouting), threadPool(std::thread::hardware_concurrency()), timeManager(threadPool), engine(*this, test) {}
Global::Global(IFileSystem* fs, bool test) : threadPool(std::thread::hardware_concurrency()), timeManager(threadPool), engine(*this, fs, test) {}

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
Interface* Global::addInterface(InterfaceType interfaceType, std::string outInterface, const size_t inQueSiz, const size_t outQueSiz, std::string mac, float interfaceId, bool debug)
{
    uint32_t key = calculateInterfaceKey(interfaceType, interfaceId);
    if (interfaceList.find(key) != interfaceList.end())
    {
        return nullptr;
    }

    InterfaceCreation iface = {interfaceType, interfaceId, *getRoutingInstance("default"), outInterface.c_str(), reinterpret_cast<const uint8_t*>(mac.data()), debug};
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
    if (interfaceList.find(key) != interfaceList.end())
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
