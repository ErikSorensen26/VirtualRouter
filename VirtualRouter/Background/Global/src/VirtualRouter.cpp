// VirtualRouter.cpp

#include <VirtualRouter.h>
#include <Eigrp.h>
#include <RoutingTable.h>
#include <Interface.h>
#include <Global.h>

// Destructor
VirtualRouter::~VirtualRouter()
{
    // Interfaces
    for (auto& [type, typeList] : interfaceList)
    {
        for (auto it = typeList.begin(); it != typeList.end();)
        {
            delete it->second;
            it->second = nullptr;
            it = typeList.erase(it);
        }
    }
    // Eigrp Autonomous Systems
    for (auto it = eigrpList.begin(); it != eigrpList.end();)
    {
        if (it->second->ipv4)
        {
            delete it->second->ipv4;
        }
        if (it->second->ipv6)
        {
            delete it->second->ipv6;
        }
        delete it->second;
        it->second = nullptr;
        it = eigrpList.erase(it);
    }
    // Eigrp Named
    for (auto it = namedEigrpList.begin(); it != namedEigrpList.end();)
    {
        delete it->second;
        it->second = nullptr;
        it = namedEigrpList.erase(it);
    }
}

// Interfaces
Interface* VirtualRouter::addInterface(Interface* interface, InterfaceType type, float interfaceId)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (interfaceList[type].find(interfaceId) != interfaceList[type].end())
    {
        return nullptr;
    }
    interfaceList[type][interfaceId] = interface;
    return interfaceList[type][interfaceId];
}

Interface* VirtualRouter::getInterface(InterfaceType type, float interfaceID)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    if (interfaceList[type].find(interfaceID) != interfaceList[type].end())
    {
        return interfaceList[type][interfaceID];
    }
    return nullptr;
}

bool VirtualRouter::removeInterface(InterfaceType type, float interfaceId)
{
    std::lock_guard<std::mutex> lock(interfaceMutex);
    auto& interfaceTypeList = interfaceList[type];
    if (interfaceTypeList.find(interfaceId) != interfaceTypeList.end())
    {
        interfaceTypeList.erase(interfaceId);
        return true;
    }
    return false;
}

void VirtualRouter::forEachInterface(const std::function<void(InterfaceType, float, Interface*)>& func, const std::vector<InterfaceType>& types, bool include)
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

// Eigrp Autonomous Systems
Protocol::EigrpAutonomousSystem* VirtualRouter::addEigrpAutonomousSystem(uint32_t id)
{
    std::lock_guard<std::mutex> lock(eigrpAutonomousSystemMutex);
    if (eigrpList.find(id) != eigrpList.end())
    {
        return nullptr;
    }
    eigrpList[id] = new Protocol::EigrpAutonomousSystem();
    return eigrpList[id];
}

Protocol::EigrpAutonomousSystem* VirtualRouter::getEigrpAutonomousSystem(uint32_t id)
{
    std::lock_guard<std::mutex> lock(eigrpAutonomousSystemMutex);
    if (eigrpList.find(id) != eigrpList.end())
    {
        return eigrpList[id];
    }
    return nullptr;
}

bool VirtualRouter::removeEigrpAutonomousSystem(uint32_t id)
{
    std::lock_guard<std::mutex> lock(eigrpAutonomousSystemMutex);
    if (eigrpList.find(id) != eigrpList.end())
    {
        delete eigrpList[id];
        eigrpList.erase(id);
        return true;
    }
    return false;
}

void VirtualRouter::forEachEigrpAutonomousSystem(const std::function<void(uint32_t, Protocol::EigrpAutonomousSystem*)>& func)
{
    std::lock_guard<std::mutex> lock(eigrpAutonomousSystemMutex);
    for (const auto& [id, system] : eigrpList)
    {
        func(id, system);
    }
}

// Eigrp Named Systems
Protocol::EigrpNamed* VirtualRouter::addEigrpNamed(const std::string& name)
{
    std::lock_guard<std::mutex> lock(eigrpNamedMutex);
    if (namedEigrpList.find(name) != namedEigrpList.end())
    {
        return nullptr;
    }
    namedEigrpList[name] = new Protocol::EigrpNamed();
    return namedEigrpList[name];
}

Protocol::EigrpNamed* VirtualRouter::getEigrpNamed(const std::string& name)
{
    std::lock_guard<std::mutex> lock(eigrpNamedMutex);
    if (namedEigrpList.find(name) != namedEigrpList.end())
    {
        return namedEigrpList[name];
    }
    return nullptr;
}

bool VirtualRouter::removeEigrpNamed(const std::string& name)
{
    std::lock_guard<std::mutex> lock(eigrpNamedMutex);
    if (namedEigrpList.find(name) != namedEigrpList.end())
    {
        auto eigrp = namedEigrpList[name];
        if (eigrp->ipv4)
        {
            uint32_t as = eigrp->ipv4->asNumber;
            if (eigrpList.find(as) != eigrpList.end())
            {
                delete eigrpList[as]->ipv4;
                eigrpList[as]->ipv4 = nullptr;
                eigrpList[as]->ipv4Named = false;
                if (!eigrpList[as]->ipv6)
                {
                    removeEigrpAutonomousSystem(as);
                }
            }
        }
        if (eigrp->ipv6)
        {
            uint32_t as = eigrp->ipv6->asNumber;
            if (eigrpList.find(as) != eigrpList.end())
            {
                delete eigrpList[as]->ipv6;
                eigrpList[as]->ipv6 = nullptr;
                eigrpList[as]->ipv6Named = false;
                if (!eigrpList[as]->ipv4)
                {
                    removeEigrpAutonomousSystem(as);
                }
            }

        }
        delete namedEigrpList[name];
        namedEigrpList.erase(name);
        return true;
    }
    return false;
}

void VirtualRouter::forEachEigrpNamed(const std::function<void(std::string, Protocol::EigrpNamed*)> func)
{
    std::lock_guard<std::mutex> lock(eigrpNamedMutex);
    for (const auto& [name, system] : namedEigrpList)
    {
        func(name, system);
    }
}
