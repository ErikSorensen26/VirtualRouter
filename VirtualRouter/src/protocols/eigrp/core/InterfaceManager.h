// InterfaceTable.h

#ifndef EIGRP_INTERFACE_MANAGER_H
#define EIGRP_INTERFACE_MANAGER_H

#include <cstdint>
#include <unordered_map>

#include "configs/RegistryReference.hpp"
#include "configs/registry/router/EigrpInterfaceRegistry.h"

class Interface;
struct IPAddress;

namespace EIGRP
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
    Config::Reference<Config::EigrpInterfaceRegistry> getRegistry(Interface& iface);
    Config::Reference<Config::EigrpInterfaceRegistry> getRegistryByKey(uint32_t key);

    // Lists
    std::unordered_map<uint32_t, EigrpInterface> eigrpInterfaceList; ///< Map of EIGRP interfaces by identifier.

private:

    Eigrp& base;
};
}

#endif // INTERFACE_MANAGER_H
