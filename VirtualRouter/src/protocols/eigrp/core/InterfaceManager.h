// InterfaceTable.h

#ifndef EIGRP_INTERFACE_MANAGER_H
#define EIGRP_INTERFACE_MANAGER_H

#include <unordered_map>
#include <map>
#include <shared_mutex>

namespace EigrpConfigs
{
struct InterfaceConfigs;
}

class Interface;
struct IPAddress;

namespace Eigrp
{
class Eigrp;
class EigrpInterface;

class InterfaceManager
{
public:
    InterfaceManager(Eigrp& base);
    ~InterfaceManager();
    
    EigrpInterface* createInterface(Interface* interface);
    void refreshInterfaceList();

    void deactivateAll();

    EigrpInterface* getInterface(uint32_t key);

    // Lists
    std::map<uint32_t, EigrpInterface> eigrpInterfaceList; ///< Map of EIGRP interfaces by identifier.
    std::unordered_map<uint32_t, EigrpConfigs::InterfaceConfigs> eigrpInterfaceConfigList; ///< Map of EIGRP interface config by identifier.
    std::shared_mutex interfaceMutex;

public:
    Eigrp& base;
};
}

#endif // INTERFACE_MANAGER_H
