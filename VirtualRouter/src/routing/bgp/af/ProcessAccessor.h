/**
 * @file ProcessAccessor.h
 * @brief Friend-based accessor that gives `AddressFamilyInstance` (and other
 *        AF-layer types) controlled access to `BgpProcess` internals without
 *        creating a circular include dependency.
 */

#ifndef BGP_PROCESS_ACCESSOR_H
#define BGP_PROCESS_ACCESSOR_H

#include <cstdint>

#include "configs/registry/router/BgpRegistry.h"

namespace core { class VirtualRouter; class ProcessQueueRef; }

namespace routing::bgp
{
class NeighborTable;
class BgpProcess;
class Neighbor;
class AttributeManager;

/**
 * @brief Stateless accessor façade for `BgpProcess` internals.
 *
 * `BgpProcess` declares `ProcessAccessor` as a friend.  All members are
 * `static`, so the class is never instantiated; it exists solely as a
 * namespace-scoped collection of accessor functions.
 *
 * ## Architectural Role
 * `AddressFamilyInstance` must read AS number, router-ID, the neighbour table,
 * the attribute manager, and the process scheduler from its owning `BgpProcess`.
 * Doing so via direct member access would require `AddressFamilyInstance.h` to
 * include `BgpProcess.h`, creating a circular dependency.  `ProcessAccessor`
 * breaks this cycle: its declarations live here (no `BgpProcess.h` needed), and
 * its definitions live in a `.cpp` that includes both headers.
 *
 * ## Lifecycle & Ownership
 * Not instantiated.  All methods are `static`.
 *
 * ## Concurrency Model
 * Callers must hold the BGP scheduler context before calling any accessor.
 *
 * @see BgpProcess
 * @ingroup BGP_AF
 */
class ProcessAccessor
{
public:
    /**
     * @brief Returns the `VirtualRouter` (VRF) associated with `proc`.
     * @param proc  The `BgpProcess` to query.
     */
    static core::VirtualRouter& getRoutingInstance(BgpProcess& proc);

    /**
     * @brief Returns the neighbour table owned by `proc`.
     * @param proc  The `BgpProcess` to query.
     */
    static NeighborTable& getNtable(BgpProcess& proc);

    /**
     * @brief Returns the local AS number configured on `proc`.
     * @param proc  The `BgpProcess` to query.
     * @return 32-bit AS number (plain or four-octet).
     */
    static uint32_t getAsNum(BgpProcess& proc);

    /**
     * @brief Returns the BGP router-ID of `proc`.
     * @param proc  The `BgpProcess` to query.
     * @return 32-bit router-ID in host byte order.
     */
    static uint32_t getRid(BgpProcess& proc);

    /**
     * @brief Returns the top-level BGP configuration registry for `proc`.
     * @param proc  The `BgpProcess` to query.
     */
    static config::BgpRegistry& getConfigs(BgpProcess& proc);

    /**
     * @brief Returns the shared `AttributeManager` owned by `proc`.
     * @param proc  The `BgpProcess` to query.
     */
    static AttributeManager& getAttrMgr(BgpProcess& proc);

    /**
     * @brief Returns a `ProcessQueueRef` pointing to the BGP scheduler of `proc`.
     *
     * The returned reference may be used to post and cancel timer callbacks that
     * run on the BGP process thread.
     *
     * @param proc  The `BgpProcess` to query.
     */
    static core::ProcessQueueRef getScheduler(BgpProcess& proc);
};
} // namespace routing

#endif // BGP_PROCESS_ACCESSOR_H

