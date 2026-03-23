// InterfaceTable.h

#ifndef EIGRP_INTERFACE_MANAGER_H
#define EIGRP_INTERFACE_MANAGER_H

#include <cstdint>
#include <unordered_map>

#include "configs/RegistryReference.hpp"
#include "configs/registry/router/EigrpInterfaceRegistry.h"

namespace interface { class Interface; }
namespace types { struct IPAddress; }

namespace routing::eigrp
{
class Eigrp;
class EigrpInterface;

class InterfaceManager
{
public:
    InterfaceManager(Eigrp& base);
    ~InterfaceManager();

    EigrpInterface* createInterface(interface::Interface* interface);
    void refreshInterfaceList();

    void deactivateAll();

    EigrpInterface* getInterface(uint32_t key);
    config::Reference<config::EigrpInterfaceRegistry> getRegistry(interface::Interface& iface);
    config::Reference<config::EigrpInterfaceRegistry> getRegistryByKey(uint32_t key);

    // Lists
    std::unordered_map<uint32_t, EigrpInterface> eigrpInterfaceList; ///< Map of EIGRP interfaces by identifier.

private:

    Eigrp& base;
};
} // namespace routing

#endif // INTERFACE_MANAGER_H

