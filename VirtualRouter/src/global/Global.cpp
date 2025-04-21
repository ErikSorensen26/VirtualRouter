#include <Global.h>
#include <Interface.h>
#include <RoutingTable.h>

#include <string>
#include <map>
#include <mutex>

// Interfaces
Interface* Global::addInterface(InterfaceType interfaceType, std::string outInterface, const size_t inQueSiz, const size_t outQueSiz, std::string mac, float interfaceId, bool debug)
{
    if (interfaceList.find({interfaceType, interfaceId}) != interfaceList.end())
    {
        return nullptr;
    }
    interfaceList[{interfaceType, interfaceId}] = new Interface(interfaceType, outInterface, inQueSiz, outQueSiz, mac, interfaceId, getRoutingInstance("default"), debug);
    interfaceList[{interfaceType, interfaceId}]->startThreads();

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

bool Global::removeInterface(InterfaceType type, float interfaceId)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (interfaceList.find({type, interfaceId}) != interfaceList.end())
    {
        delete interfaceList[{type, interfaceId}];
        interfaceList[{type, interfaceId}] = nullptr;
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
    routingInstances[name] = new VirtualRouter(name);
    return routingInstances[name];
}

VirtualRouter* Global::getRoutingInstance(const std::string& name)
{
    std::lock_guard<std::mutex> lock(routingInstanceMutex);
    if (routingInstances.find(name) != routingInstances.end())
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
