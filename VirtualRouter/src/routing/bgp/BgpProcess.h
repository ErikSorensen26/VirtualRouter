/**
 * @file BgpProcess.h
 * @brief Top-level BGP process: one per configured AS, owns a BgpScope per VRF.
 * @ingroup BGP
 */

#ifndef BGP_PROCESS_H
#define BGP_PROCESS_H

#include "configs/registry/router/BgpRegistry.h"
#include "BgpTypes.hpp"
#include "af/DirtyState.hpp"
#include "attributes/AttributeManager.hpp"
#include "neighbor/PeerTemplate.h"

namespace core { class Global; }

class Internal_BgpTest;

namespace routing::bgp
{
class BgpScope;

/**
 * @brief Top-level object for one configured AS; owns one BgpScope per VRF it runs in.
 * @ingroup BGP
 *
 * A single BGP process can run in multiple VRFs simultaneously (each `network`,
 * neighbor, or address-family config can target a different VRF). BgpProcess is
 * the router-wide anchor for that AS; @ref BgpScope is the actual per-VRF runtime
 * state (neighbors, sessions, RIBs).
 *
 * ## Architectural Role
 * The BGP config registry (@ref config::BgpRegistry) applies against this object,
 * not against individual scopes — registry appliers cast their config-apply
 * context to `BgpProcess*` (see `BgpRegistry.cpp`'s "Process-level" appliers).
 * Each `enqueue*` method is the fan-out point from that single config-apply
 * event to every affected `BgpScope`.
 *
 * ## Lifecycle & Ownership
 * `scopes` is keyed by VRF name and populated lazily: @ref enqueueAddressFamily
 * creates a `BgpScope` the first time an AF is enabled for a given VRF, and
 * erases it once its last AF is disabled. Peer groups, session templates, and
 * policy templates (`peerTemplates`) are process-wide — shared across every VRF
 * this AS runs in, not duplicated per scope.
 *
 * ## Concurrency Model
 * BgpProcess itself has no scheduler of its own. Each `enqueue*` method posts
 * work onto the scheduler of the affected `BgpScope`(s), so the actual mutation
 * happens on that scope's own thread, not on the caller's. `scopes` and
 * `peerTemplates` are read here directly (not posted), so `enqueue*` calls and
 * anything that creates/destroys a scope must run on the same thread — normally
 * the CLI/config-apply thread.
 *
 * @see BgpScope
 */
class BgpProcess
{
public:
    /**
     * @brief Constructs the process for the given AS.
     *
     * Resolves the router ID from `reg`'s BGP_ROUTER_ID if set, otherwise
     * calculates it from the default VRF.
     *
     * @param reg    Process-level BGP configuration registry.
     * @param as     Local AS number.
     * @param global Owning system controller; used to resolve VRFs by name.
     */
    BgpProcess(const config::BgpRegistry& reg, uint32_t as, core::Global& global);

    /**
     * @brief Destructs the process, tearing down every owned BgpScope.
     *
     * Resets the config context, then clears `scopes`. Each BgpScope destructor
     * runs its own ordered teardown (stop listener, cancel timers, notify peers,
     * destroy sessions and address families) before this returns.
     */
    ~BgpProcess();

    uint32_t getRouterId() const;

    void enqueueNeighbor(config::BgpNeighborSessionRegistry* cfgs, const types::IPAddress& addr);
    void enqueueSyncPeerGroup(config::BgpNeighborSessionRegistry* cfgs, const std::string& group);
    void enqueueSyncPeerSessionTemplate(config::BgpNeighborSessionRegistry* cfgs, const std::string& sess);
    void enqueueSyncPeerPolicyTemplate(config::BgpNeighborRegistry* cfgs, const std::string& policy);
    void enqueueAddressFamily(const AfiSafi& afiSafi, config::BgpAddressFamilyRegistry* cfgs, const std::string& vrf);
    void enqueueMarkAllAfDirty(AfDirty category);
    void enqueueMarkAllInbound(InDirty category);
    void enqueueMarkAllOutbound(OutAttr attr);
    void enqueueRestartAllSessions();
    void enqueueSyncConfederation();

    core::Global& global; ///< Owning system controller; set at construction. Used to resolve VRFs by name.

    const uint32_t asNumber; ///< Local AS number — immutable after construction.

private:
    friend class BgpScope;
    friend class PeerTemplateTable;
    friend class ::Internal_BgpTest;
    friend class BgpRx; // resolves findAddressFamily() while dispatching inbound UPDATEs

    AttributeManager attrMgr;     ///< Flyweight store for path attributes shared across all sessions.
    PeerTemplateTable peerTemplates; ///< Peer groups and session/policy templates for this scope.

    const config::BgpRegistry& configs;         ///< Process-level BGP configuration.

    std::unordered_map<std::string, BgpScope> scopes;

    uint32_t rid;
};
}

#endif // BGP_PROCESS_H
