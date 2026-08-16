/**
 * @file ScopeAccessor.h
 * @brief Friend-based accessor that gives `AddressFamilyInstance` (and other
 *        AF-layer types) controlled access to `BgpScope` internals without
 *        creating a circular include dependency.
 */

#ifndef BGP_SCOPE_ACCESSOR_H
#define BGP_SCOPE_ACCESSOR_H

#include <cstdint>

#include "configs/registry/router/BgpRegistry.h"

namespace core { class VirtualRouter; class ProcessQueue; }

namespace routing::bgp
{
class NeighborTable;
class BgpScope;
class Neighbor;
class AttributeManager;

/**
 * @brief Stateless accessor façade for `BgpScope` internals.
 *
 * `BgpScope` declares `ScopeAccessor` as a friend.  All members are
 * `static`, so the class is never instantiated; it exists solely as a
 * namespace-scoped collection of accessor functions.
 *
 * ## Architectural Role
 * `AddressFamilyInstance` must read AS number, router-ID, the neighbour table,
 * the attribute manager, and the scope's scheduler from its owning `BgpScope`.
 * Doing so via direct member access would require `AddressFamilyInstance.h` to
 * include `BgpScope.h`, creating a circular dependency.  `ScopeAccessor`
 * breaks this cycle: its declarations live here (no `BgpScope.h` needed), and
 * its definitions live in a `.cpp` that includes both headers.
 *
 * ## Lifecycle & Ownership
 * Not instantiated.  All methods are `static`.
 *
 * ## Concurrency Model
 * Callers must hold the BGP scheduler context before calling any accessor.
 *
 * @see BgpScope
 * @ingroup BGP_AF
 */
class ScopeAccessor
{
public:
    /**
     * @brief Returns the `VirtualRouter` (VRF) associated with `scope`.
     * @param scope  The `BgpScope` to query.
     */
    static core::VirtualRouter& getRoutingInstance(BgpScope& scope);

    /**
     * @brief Returns the neighbour table owned by `scope`.
     * @param scope  The `BgpScope` to query.
     */
    static NeighborTable& getNtable(BgpScope& scope);

    /**
     * @brief Returns the local AS number configured on `scope`.
     * @param scope  The `BgpScope` to query.
     * @return 32-bit AS number (plain or four-octet).
     */
    static uint32_t getAsNum(BgpScope& scope);

    /**
     * @brief Returns the BGP router-ID of `scope`.
     * @param scope  The `BgpScope` to query.
     * @return 32-bit router-ID in host byte order.
     */
    static uint32_t getRid(BgpScope& scope);

    /**
     * @brief Returns the BGP configuration registry for `scope`.
     * @param scope  The `BgpScope` to query.
     */
    static const config::BgpRegistry& getConfigs(BgpScope& scope);

    /**
     * @brief Returns the shared `AttributeManager` owned by `scope`.
     * @param scope  The `BgpScope` to query.
     */
    static AttributeManager& getAttrMgr(BgpScope& scope);

    /**
     * @brief Returns the lifetime-safe self-ref of `scope`'s scheduler.
     *
     * The returned reference may be used to post and cancel timer callbacks that
     * run on the BGP scope's thread. It is released first in `~BgpScope()`.
     *
     * @param scope  The `BgpScope` to query.
     */
    static core::ProcessQueue& getScheduler(BgpScope& scope);
};
} // namespace routing

#endif // BGP_SCOPE_ACCESSOR_H
