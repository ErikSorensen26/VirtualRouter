#include <Global.h>
#include <Interface.h>
#include <RoutingTable.h>
#include <VirtualRouter.h>

#include <string>
#include <map>
#include <mutex>

Global::Global(const StartupFiles& stfs, bool enableRouting, bool test) : routingEnabled(enableRouting), threadPool(std::thread::hardware_concurrency()), timeManager(threadPool), engine(*this, stfs, test) {}
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
Interface* Global::addInterface(InterfaceType interfaceType, std::string outInterface, const size_t inQueSiz, const size_t outQueSiz, std::string mac, float interfaceId, bool debug)
{
    if (interfaceList.find({interfaceType, interfaceId}) != interfaceList.end())
    {
        return nullptr;
    }
    interfaceList[{interfaceType, interfaceId}] = new Interface(interfaceType, outInterface, inQueSiz, outQueSiz, mac, interfaceId, *getRoutingInstance("default"), debug);

    return interfaceList[{interfaceType, interfaceId}];
}

Interface* Global::getInterface(InterfaceType type, float interfaceID)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (interfaceList.find({type, interfaceID}) != interfaceList.end())
    {
        return interfaceList[{type, interfaceID}];
    }
    return nullptr;
}

std::map<std::pair<InterfaceType, float>, Interface*> Global::getInterfaceList()
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    return interfaceList;
}

bool Global::removeInterface(InterfaceType type, float interfaceId)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (interfaceList.find({type, interfaceId}) != interfaceList.end())
    {
        interfaceList.erase({type, interfaceId});
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
