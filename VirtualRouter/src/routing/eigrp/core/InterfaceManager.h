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
 * @brief Tracks and creates @ref EigrpInterface objects for a single EIGRP process.
 * @ingroup EIGRP_CORE
 *
 * `InterfaceManager` is the bridge between the system-level
 * @ref interface::Interface objects and the EIGRP-level @ref EigrpInterface
 * objects. The owning @ref Eigrp process subscribes to `IF_READY`/`IF_DOWN`
 * events on the per-VRF @ref interface::InterfaceManager; each event is
 * handled as a single, targeted create or destroy for the interface that
 * fired it -- there is no bulk sweep or reconciliation pass.
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
     * @param process The @ref Eigrp process that owns this manager.
     */
    InterfaceManager(Eigrp& process);

    /**
     * @brief Destructs the manager.
     *
     * Callers must invoke `deactivateAll()` before destruction to ensure all
     * timer callbacks are cancelled.
     */
    ~InterfaceManager();

    /**
     * @brief Creates the AF_INTERFACE registry slot for a physical interface.
     *
     * Called when the owning @ref Eigrp process observes `IF_READY` for
     * `interface`. Firing `emplaceBack()` synchronously invokes the
     * AF_INTERFACE applier (EigrpRegistry.cpp), which calls back into
     * `createInterface(key, registry&)` to construct the @ref EigrpInterface.
     *
     * No-op if an entry for this interface already exists.
     *
     * @param interface Physical interface to enable EIGRP on; must not be null.
     */
    void tryCreateInterface(interface::Interface& interface);

    /**
     * @brief Constructs the @ref EigrpInterface for an already-created
     *        AF_INTERFACE registry slot and inserts it into `eigrpInterfaceList`.
     *
     * Called by the AF_INTERFACE applier once the registry slot exists; not
     * for use as a general entry point -- `addInterface()` is that entry point.
     *
     * @param key    Interface lookup key.
     * @param cfg    Registry slot to bind the new EigrpInterface to.
     * @return Pointer to the newly-constructed EigrpInterface, or nullptr if
     *         the physical interface can no longer be found or the entry
     *         already exists.
     */
    EigrpInterface* createInterface(interface::InterfaceKey key, config::EigrpInterfaceRegistry& cfg);

    /**
     * @brief Erases the EigrpInterface for an AF_INTERFACE key already
     *        removed from the registry.
     *
     * Called by the AF_INTERFACE applier once the registry slot is gone;
     * not for use as a general entry point -- `removeInterface()` is that
     * entry point.
     *
     * @param key Lookup key for the interface.
     * @return True if an EigrpInterface was erased, false if not found.
     */
    bool destroyInterface(interface::InterfaceKey key);

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

    // INTERFACE LIST
    std::unordered_map<interface::InterfaceKey, EigrpInterface> eigrpInterfaceList; ///< Active EIGRP interfaces keyed by interface identifier.

private:

    Eigrp& process; ///< Owning EIGRP process.
};
} // namespace routing::eigrp

#endif // INTERFACE_MANAGER_H

