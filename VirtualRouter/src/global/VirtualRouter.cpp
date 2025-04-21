// VirtualRouter.cpp

#include <VirtualRouter.h>
#include <Eigrp.h>
#include <RoutingTable.h>
#include <Interface.h>
#include <Global.h>

// Destructor
VirtualRouter::~VirtualRouter()
{
    {
        std::unique_lock<std::shared_mutex> lock(interfaceMutex);
        // Interfaces
        for (auto it = interfaceList.begin(); it != interfaceList.end();)
        {
            delete it->second;
            it->second = nullptr;
        }
        interfaceList.clear();
    }

    {
        std::unique_lock<std::shared_mutex> lock(eigrpAutonomousSystemMutex);
        // Eigrp Autonomous Systems
        for (auto it : eigrpList)
        {
            if (it.second->ipv4)
            {
                delete it.second->ipv4;
            }
            if (it.second->ipv6)
            {
                delete it.second->ipv6;
            }
            delete it.second;
            it.second = nullptr;
        }
        eigrpList.clear();
    }

    {
        std::unique_lock<std::shared_mutex> lock(eigrpNamedMutex);
        // Eigrp Named
        for (auto it : namedEigrpList)
        {
            delete it.second;
            it.second = nullptr;
        }
        namedEigrpList.clear();
    }
}

// Interfaces
Interface* VirtualRouter::addInterface(Interface* interface, InterfaceType type, float interfaceId)
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    if (interfaceList.find({type, interfaceId}) != interfaceList.end())
    {
        return nullptr;
    }
    interfaceList[{type, interfaceId}] = interface;
    return interfaceList[{type, interfaceId}];
}

Interface* VirtualRouter::getInterface(InterfaceType type, float interfaceID)
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    if (interfaceList.find({type, interfaceID}) != interfaceList.end())
    {
        return interfaceList[{type, interfaceID}];
    }
    return nullptr;
}

bool VirtualRouter::removeInterface(InterfaceType type, float interfaceId)
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    if (interfaceList.find({type, interfaceId}) != interfaceList.end())
    {
        interfaceList.erase({type, interfaceId});
        return true;
    }
    return false;
}

// Eigrp Autonomous Systems
Protocol::EigrpAutonomousSystem* VirtualRouter::addEigrpAutonomousSystem(uint32_t id)
{
    std::shared_lock<std::shared_mutex> lock(eigrpAutonomousSystemMutex);
    if (eigrpList.find(id) != eigrpList.end())
    {
        return nullptr;
    }
    eigrpList[id] = new Protocol::EigrpAutonomousSystem();
    return eigrpList[id];
}

Protocol::EigrpAutonomousSystem* VirtualRouter::getEigrpAutonomousSystem(uint32_t id)
{
    std::shared_lock<std::shared_mutex> lock(eigrpAutonomousSystemMutex);
    if (eigrpList.find(id) != eigrpList.end())
    {
        return eigrpList[id];
    }
    return nullptr;
}

bool VirtualRouter::removeEigrpAutonomousSystem(uint32_t id)
{
    std::shared_lock<std::shared_mutex> lock(eigrpAutonomousSystemMutex);
    if (eigrpList.find(id) != eigrpList.end())
    {
        delete eigrpList[id];
        eigrpList.erase(id);
        return true;
    }
    return false;
}

// Eigrp Named Systems
Protocol::EigrpNamed* VirtualRouter::addEigrpNamed(const std::string& name)
{
    std::shared_lock<std::shared_mutex> lock(eigrpNamedMutex);
    if (namedEigrpList.find(name) != namedEigrpList.end())
    {
        return nullptr;
    }
    namedEigrpList[name] = new Protocol::EigrpNamed();
    return namedEigrpList[name];
}

Protocol::EigrpNamed* VirtualRouter::getEigrpNamed(const std::string& name)
{
    std::shared_lock<std::shared_mutex> lock(eigrpNamedMutex);
    if (namedEigrpList.find(name) != namedEigrpList.end())
    {
        return namedEigrpList[name];
    }
    return nullptr;
}

bool VirtualRouter::removeEigrpNamed(const std::string& name)
{
    std::shared_lock<std::shared_mutex> lock(eigrpNamedMutex);
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
