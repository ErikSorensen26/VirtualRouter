#include <Global.h>
#include <Interface.h>
#include <RoutingTable.h>

#include <string>
#include <map>
#include <mutex>

// Interfaces
Interface* Global::addInterface(InterfaceType interfaceType, std::string outInterface, const size_t inQueSiz, const size_t outQueSiz, std::string mac, float interfaceId, bool debug)
{
    if (interfaceList[interfaceType].find(interfaceId) != interfaceList[interfaceType].end())
    {
        return nullptr;
    }
    interfaceList[interfaceType][interfaceId] = new Interface(interfaceType, outInterface, inQueSiz, outQueSiz, mac, interfaceId, getRoutingInstance("default"), debug);
    return interfaceList[interfaceType][interfaceId];
}

Interface* Global::getInterface(InterfaceType type, float interfaceID)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (interfaceList[type].find(interfaceID) != interfaceList[type].end())
    {
        return interfaceList[type][interfaceID];
    }
    return nullptr;
}

std::map<float, Interface*>* Global::getInterfaceType(InterfaceType type)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    return &(interfaceList[type]);
}

bool Global::removeInterface(InterfaceType type, float interfaceId)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    auto& interfaceTypeList = interfaceList[type];
    if (interfaceTypeList.find(interfaceId) != interfaceTypeList.end())
    {
        delete interfaceTypeList[interfaceId];
        interfaceTypeList[interfaceId] = nullptr;
        interfaceTypeList.erase(interfaceId);
        return true;
    }
    return false;
}

void Global::forEachInterface(const std::function<void(InterfaceType, float, Interface*)>& func, const std::vector<InterfaceType>& types, bool include)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    for (const auto& [type, innermap] : interfaceList)
    {
        if (!types.empty())
        {
            if (include && std::find(types.begin(), types.end(), type) == types.end())
            {
                continue;
            }
            else if (!include && std::find(types.begin(), types.end(), type) != types.end())
            {
                continue;
            }
        }
        for (const auto& [index, iface] : innermap)
        {
            func(type, index, iface);
        }
    }
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
