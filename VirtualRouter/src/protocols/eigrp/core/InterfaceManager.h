// InterfaceTable.h

#ifndef EIGRP_INTERFACE_MANAGER_H
#define EIGRP_INTERFACE_MANAGER_H

#include <cstdint>
#include <map>

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

    // Lists
    std::map<uint32_t, EigrpInterface> eigrpInterfaceList; ///< Map of EIGRP interfaces by identifier.

private:
    // Per-interface registry storage (for named mode or when not provided by interface)
    std::map<uint32_t, Config::Reference<Config::EigrpInterfaceRegistry>> ifaceRegistryList;

    Eigrp& base;
};
}

#endif // INTERFACE_MANAGER_H
