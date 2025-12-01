// VirtualRouter.cpp

#include <VirtualRouter.h>
#include <Eigrp.h>
#include <Interface.h>
#include <Global.h>

// Destructor
VirtualRouter::~VirtualRouter()
{
    {
        std::unordered_map<uint32_t, Interface*> interfaceListCopy;
        {
            // Move out interfaces so any callbacks during destruction
            // do not see stale pointers in the shared map.
            std::unique_lock<std::shared_mutex> lock(interfaceMutex);
            interfaceListCopy.swap(interfaceList);
        }
        // Interfaces
        for (auto [key, interface] : interfaceListCopy)
        {
            global.removeInterface(key);
        }
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
Interface* VirtualRouter::addInterface(Interface* interface, uint32_t key)
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    if (interfaceList.find(key) != interfaceList.end())
    {
        return nullptr;
    }
    interfaceList[key] = interface;
    return interfaceList[key];
}

Interface* VirtualRouter::getInterface(uint32_t key)
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    if (interfaceList.find(key) != interfaceList.end())
    {
        return interfaceList[key];
    }
    return nullptr;
}

std::unordered_map<uint32_t, Interface*> VirtualRouter::getinterfaceList()
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    return interfaceList;
}

bool VirtualRouter::removeInterface(uint32_t key)
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    if (interfaceList.find(key) != interfaceList.end())
    {
        interfaceList.erase(key);
        return true;
    }
    return false;
}

// Eigrp Autonomous Systems
Eigrp::EigrpAutonomousSystem* VirtualRouter::addEigrpAutonomousSystem(uint32_t id)
{
    std::shared_lock<std::shared_mutex> lock(eigrpAutonomousSystemMutex);
    if (eigrpList.find(id) != eigrpList.end())
    {
        return nullptr;
    }
    eigrpList[id] = new Eigrp::EigrpAutonomousSystem();
    return eigrpList[id];
}

Eigrp::EigrpAutonomousSystem* VirtualRouter::getEigrpAutonomousSystem(uint32_t id)
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
Eigrp::EigrpNamed* VirtualRouter::addEigrpNamed(const std::string& name)
{
    std::shared_lock<std::shared_mutex> lock(eigrpNamedMutex);
    if (namedEigrpList.find(name) != namedEigrpList.end())
    {
        return nullptr;
    }
    namedEigrpList[name] = new Eigrp::EigrpNamed();
    return namedEigrpList[name];
}

Eigrp::EigrpNamed* VirtualRouter::getEigrpNamed(const std::string& name)
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
            uint32_t as = eigrp->ipv4->getAS();
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
            uint32_t as = eigrp->ipv6->getAS();
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
