/**
 * @file InterfaceManager.h
 * @brief Manages the set of EIGRP-enabled interfaces for a single process.
 */

#ifndef EIGRP_INTERFACE_MANAGER_H
#define EIGRP_INTERFACE_MANAGER_H

#include <cstdint>
#include <unordered_map>

#include "configs/RegistryReference.hpp"
#include "configs/registry/router/EigrpInterfaceRegistry.h"

#include "interface/configs/InterfaceType.hpp"
namespace interface { class Interface; }
namespace types { struct IPAddress; }

namespace routing::eigrp
{
class Eigrp;
class EigrpInterface;

/**
 * @brief Tracks and creates @ref EigrpInterface objects for all interfaces
 *        that match the process's `network` statements.
 * @ingroup EIGRP_CORE
 *
 * `InterfaceManager` is the bridge between the system-level
 * @ref interface::Interface objects and the EIGRP-level @ref EigrpInterface
 * objects.  When the owning @ref Eigrp process calls `refreshInterfaceList()`,
 * this class queries the VRF for all live interfaces, checks each against the
 * configured network ranges, and creates or destroys `EigrpInterface` entries
 * accordingly.
 *
 * ## Architectural Role
 * Owned by `Eigrp`. Does not perform packet I/O; that is the responsibility
 * of each `EigrpInterface`'s @ref ReliableTransport.
 *
 * ## Lifecycle & Ownership
 * `EigrpInterface` objects are stored by value in `eigrpInterfaceList`.
 * `deactivateAll()` must be called before destruction to cleanly tear down
 * neighbors and cancel timers.
 *
 * @warning Do not erase entries from `eigrpInterfaceList` while an interface's
 *          `ReliableTransport` or `InterfaceTimers` has an outstanding timer
 *          callback; the callback holds a reference to the `EigrpInterface`.
 */
class InterfaceManager
{
public:
    /**
     * @brief Constructs the manager bound to the given EIGRP process.
     *
     * @param base The @ref Eigrp process that owns this manager.
     */
    InterfaceManager(Eigrp& base);

    /**
     * @brief Destructs the manager.
     *
     * Callers must invoke `deactivateAll()` before destruction to ensure all
     * timer callbacks are cancelled.
     */
    ~InterfaceManager();

    /**
     * @brief Creates an @ref EigrpInterface for the given physical interface
     *        and inserts it into `eigrpInterfaceList`.
     *
     * If an entry for `interface` already exists, the existing pointer is
     * returned without creating a duplicate.
     *
     * @param interface Physical interface to enable EIGRP on; must not be null.
     * @return Pointer to the (possibly newly created) @ref EigrpInterface.
     */
    EigrpInterface* createInterface(interface::Interface* interface);

    /**
     * @brief Synchronizes `eigrpInterfaceList` against the current VRF
     *        interface set and configured network ranges.
     *
     * Adds `EigrpInterface` entries for newly matching interfaces and removes
     * entries for interfaces that no longer match.
     */
    void refreshInterfaceList();

    /**
     * @brief Brings down all active EIGRP interfaces, sending goodbye hellos
     *        and cancelling all timers.
     *
     * Called during process shutdown before the interface list is destroyed.
     */
    void deactivateAll();

    /**
     * @brief Looks up an @ref EigrpInterface by its interface key.
     *
     * @param key System interface identifier.
     * @return Pointer to the matching interface, or nullptr if not found.
     */
    EigrpInterface* getInterface(interface::InterfaceKey key);

    /**
     * @brief Returns a registry reference for the per-interface EIGRP
     *        configuration of the given physical interface.
     *
     * @param iface Physical interface whose registry is needed.
     */
    config::Reference<config::EigrpInterfaceRegistry> getRegistry(interface::Interface& iface);

    /**
     * @brief Returns a registry reference for the per-interface EIGRP
     *        configuration identified by key.
     *
     * @param key System interface identifier.
     */
    config::Reference<config::EigrpInterfaceRegistry> getRegistryByKey(interface::InterfaceKey key);

    // INTERFACE LIST
    std::unordered_map<interface::InterfaceKey, EigrpInterface> eigrpInterfaceList; ///< Active EIGRP interfaces keyed by interface identifier.

private:

    Eigrp& base; ///< Owning EIGRP process.
};
} // namespace routing::eigrp

#endif // INTERFACE_MANAGER_H

