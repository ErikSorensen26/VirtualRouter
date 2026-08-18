/**
 * @file NeighborTable.h
 * @brief BGP neighbor table: session state and statistics per peer.
 */

#ifndef BGP_NEIGHBOR_TABLE_H
#define BGP_NEIGHBOR_TABLE_H

#include <unordered_map>
#include <cstdint>

#include "bgp/neighbor/Neighbor.h"
#include "bgp/neighbor/PeerTemplate.h"

namespace types { struct IPAddress; }
namespace config { struct BgpRegistry; }

class Internal_BgpTest;

namespace routing::bgp
{
class BgpScope;
class Neighbor;

/**
 * @brief Manages the set of BGP neighbors and peer templates for one BGP scope.
 * @ingroup BGP_NEIGHBOR
 *
 * NeighborTable is the authoritative registry of configured BGP peers.  It maps
 * peer IP addresses to @ref Neighbor objects and maintains a secondary index by
 * router ID for fast lookup after session establishment.  It also owns all
 * peer templates (peer groups, session templates, policy templates) used to
 * share configuration across multiple neighbors.
 *
 * Dynamic neighbors created from a `bgp listen range` match are tracked in the
 * same primary map as static neighbors; they inherit their configuration from
 * the named peer group at creation time.
 *
 * ## Architectural Role
 * Owned exclusively by @ref BgpScope.  No other subsystem holds a pointer to
 * this table; callers reach individual neighbors through the owning scope.
 * Template and neighbor lifecycle is driven by configuration sync events
 * dispatched on the BGP scheduler thread.
 *
 * ## Lifecycle & Ownership
 * Constructed with a reference to the owning @ref BgpScope.  Neighbors are
 * inserted by @ref createNeighbor or @ref createDynamicNeighbor and removed by
 * @ref deleteNeighbor.  The secondary router-ID index is maintained by
 * @ref activatePeer and @ref deactivatePeer, which are called by the session
 * FSM on ESTABLISHED and session-down transitions respectively.
 *
 * ## Concurrency Model
 * All mutations must be called from the BGP scheduler thread.  The
 * @ref disableConnectionCheck flag is updated by @ref runDccCheck and read by
 * the TCP accept path, which may run on a different thread; callers must
 * synchronize access to that field externally if they need a consistent view
 * from outside the scheduler.
 *
 * @see Neighbor, PeerTemplateTable, BgpScope
 */
class NeighborTable
{
public:
    /**
     * @brief Constructs an empty neighbor table bound to @p scope.
     *
     * @param scope Owning BGP scope; used when creating new Neighbor objects
     *              and when constructing peer templates that need a scope reference.
     * @param table Peer template table for managing config inheritance.
     */
    NeighborTable(BgpScope& scope, PeerTemplateTable& table);

    /**
     * @brief Start a session for @p nbr according to its configured
     * TRANSPORT_CONNECTION_MODE (active by default, passive when explicitly
     * set to passive).
     *
     * @param nbr Neighbor to start a session for.
     */
    void startConfiguredSession(Neighbor& nbr);

    /**
     * @brief Create a statically configured BGP neighbor for @p ipAddress.
     *
     * Inserts a new @ref Neighbor into the table. Does not start a session --
     * callers that want the neighbor's configured session started (the normal
     * case for a real config-driven create) must call @ref startConfiguredSession
     * explicitly afterward. If a neighbor already exists at that address,
     * returns a pointer to the existing entry without modifying it or touching
     * its session.
     *
     * @param ipAddress Peer IP address; used as the primary key.
     * @return Pointer to the new or existing Neighbor entry.
     */
    Neighbor* createNeighbor(config::BgpNeighborSessionRegistry& cfgs, const types::IPAddress& ipAddress);

    /**
     * @brief Remove and destroy the statically configured neighbor at @p ipAddress.
     *
     * A no-op if no neighbor exists at that address, or if it is a dynamic
     * neighbor (not owned by the static config). If the neighbor has an
     * active session it must be torn down before calling this method. Also
     * removes the router-ID index entry if one was registered.
     *
     * @param ipAddress Peer IP address to remove.
     *
     * @warning The caller is responsible for ensuring the session FSM for this
     * neighbor has reached the Idle state before deletion; removing an active
     * neighbor may leave dangling pointers inside the session.
     */
    void deleteNeighbor(const types::IPAddress& ipAddress);

    /**
     * @brief Create a passive-only neighbor from a `bgp listen range` match.
     *
     * The new neighbor inherits all session and policy configuration from the
     * named peer group.  Dynamic neighbors are passive-only: they never initiate
     * outbound TCP connections.
     *
     * @param ipAddress     Peer IP address that matched the listen range.
     * @param peerGroupName Name of the peer group whose configuration is inherited.
     * @return Pointer to the newly created Neighbor, or nullptr if the peer group
     *         does not exist.
     */
    Neighbor* createDynamicNeighbor(const types::IPAddress& ipAddress, const std::string& peerGroupName);
    
    /**
     * @brief Removes every dynamically created neighbor spawned from `group`.
     *
     * Called by @ref PeerTemplateTable::removePeerGroup before the group itself is
     * erased. A dynamic neighbor's NeighborConfigs aliases its origin group's session
     * registry rather than owning one (see @ref NeighborConfigs(PeerGroup&)), so it
     * cannot outlive the group.
     *
     * @param group The dynamic peer group whose spawned neighbors should be erased.
     */
    void purgeDynamicNeighbors(PeerGroup* group);

    /**
     * @brief Look up a neighbor by peer IP address.
     *
     * @param ipAddress Peer IP address to search for.
     * @return Pointer to the Neighbor if found, nullptr otherwise.
     */
    Neighbor* lookup(const types::IPAddress& ipAddress);

    /**
     * @brief Look up a neighbor by the peer's BGP router ID.
     *
     * The router-ID index is populated only for sessions that have completed
     * the OPEN exchange; a neighbor that has not yet established will not appear
     * in this index.
     *
     * @param rid 32-bit BGP router ID received in the peer's OPEN message.
     * @return Pointer to the Neighbor if found, nullptr otherwise.
     */
    Neighbor* lookup(uint32_t rid);

    /**
     * @brief Look up a neighbor's per-AF state by peer IP address and AFI/SAFI.
     *
     * @param ipAddress Peer IP address to search for.
     * @param afi       Address family to look up on the found neighbor.
     * @return Pointer to the NeighborAf if both the neighbor and the activated
     *         address family exist, nullptr otherwise.
     */
    NeighborAf* lookup(const types::IPAddress& ipAddress, const AfiSafi& afi);

    /**
     * @brief Look up a neighbor's per-AF state by the peer's BGP router ID and AFI/SAFI.
     *
     * @param rid 32-bit BGP router ID received in the peer's OPEN message.
     * @param afi Address family to look up on the found neighbor.
     * @return Pointer to the NeighborAf if both the neighbor and the activated
     *         address family exist, nullptr otherwise.
     */
    NeighborAf* lookup(uint32_t rid, const AfiSafi& afi);

    /**
     * @brief Register the router-ID-to-neighbor mapping for an established peer.
     *
     * Called by the session FSM when the OPEN exchange completes and the peer's
     * router ID is known.  Overwrites any previous entry for @p peer.
     *
     * @param peer 32-bit router ID from the peer's OPEN message.
     * @param session Session object for peers open session.
     * @return True if the neighbor at @p nbr exists and was indexed; false if
     *         no neighbor exists at that address.
     */
    bool activatePeer(uint32_t peer, Session& sess);

    /**
     * @brief Remove the router-ID-to-neighbor mapping for a peer that went down.
     *
     * @param session Session that is being de-registered.
     * @return True if the entry existed and was removed; false if not found.
     */
    bool deactivatePeer(Session& sess);

    /**
     * @brief Cancel the hold timer on every configured neighbor.
     *
     * Used during scope shutdown to stop all pending timers before tearing
     * down sessions in an orderly fashion.
     */
    void cancelAllHoldTimers();

    /**
     * @brief Re-evaluate the DISABLE_CONNECTION_CHECK flag across all neighbors.
     *
     * Scans every neighbor's configuration and sets @ref disableConnectionCheck
     * to true if any neighbor has the flag enabled.  Called after any
     * configuration change that might affect connection-check behavior.
     */
    void runDccCheck();

    // UTILS

    /**
     * @brief Returns whether @p nbr is configured with SHUTDOWN.
     */
    bool isShutdown(const Neighbor& nbr) const;

    /**
     * @brief Returns whether @p nbr should have its TCP connection validated
     *        (eBGP, DISABLE_CONNECTION_CHECK not set, and not EBGP_MULTIHOP).
     */
    bool isConnectionCheck(const Neighbor& nbr) const;

    /**
     * @brief Returns @p nbr's configured TRANSPORT_CONNECTION_MODE as a bool
     *        (true = active), or nullopt if not configured.
     */
    std::optional<bool> isTcpConnectionMode(const Neighbor& nbr) const;

    /**
     * @brief Shuts down @p neighbor's session; delegates to @ref BgpScope::shutdownNeighbor.
     */
    void shutdownNeighbor(Neighbor& neighbor);

    /**
     * @brief Clears SHUTDOWN and lets @p neighbor re-establish; delegates to
     *        @ref BgpScope::unshutdownNeighbor.
     */
    void unshutdownNeighbor(Neighbor& neighbor);

    /**
     * @brief Reset an existing session in place; a no-op if the neighbor has no session.
     */
    void restartNeighbor(Neighbor& neighbor);

    /**
     * @brief Destroy all neighbors (and the RID index). Used during ordered teardown.
     */
    void clear();

    /**
     * @brief Looks up the scope-wide AddressFamilyVariant for @p afi;
     *        delegates to @ref BgpScope::findAddressFamily.
     */
    AddressFamilyVariant* findAddressFamily(const AfiSafi& afi);

    bool disableConnectionCheck = false; ///< Whether any configured neighbor has DISABLE_CONNECTION_CHECK enabled.

    PeerGroup* lookupPeerGroup(const std::string& name);

    void syncPeerGroup(Neighbor& nbr);
    void syncPeerGroup(NeighborAf& nbr);
    void syncPeerSessionTemplate(Neighbor& nbr);
    void syncPeerPolicyTemplate(NeighborAf& nbr);

    /**
     * @brief Invoke @p fn for every neighbor in the table.
     *
     * @tparam F Callable accepting a `Neighbor&` (or `const Neighbor&`).
     * @param fn  Function to call for each neighbor.
     */
    template <typename F>
    void forEachNeighbor(F&& fn);
    template <typename F>
    void forEachNeighbor(F&& fn) const;

    /**
     * @brief Invoke @p fn for every established peer (router-ID index).
     *
     * Only neighbors that have completed the OPEN exchange and have a registered
     * router ID appear in this iteration.
     *
     * @tparam F Callable accepting a `Neighbor&` (or `const Neighbor&`).
     * @param fn  Function to call for each established peer.
     */
    template <typename F>
    void forEachSession(F&& fn);
    template <typename F>
    void forEachSession(F&& fn) const;

private:
    friend class Neighbor;
    friend class BgpScope;
    friend class ::Internal_BgpTest;

    bool isPeerConfed(uint32_t peerAs) const;

    const config::BgpRegistry& getConfigs() const;

    BgpScope& scope;                 ///< Owning scope; passed to each new Neighbor on creation.
    PeerTemplateTable& peerTemplates; ///< Peer groups and session/policy templates for this scope.

    std::unordered_map<types::IPAddress, Neighbor> neighbors; ///< Primary neighbor store, keyed by peer IP.
    std::unordered_map<uint32_t, Neighbor*> peers;            ///< Secondary index by router ID; populated on ESTABLISHED.
};

template <typename F>
void NeighborTable::forEachNeighbor(F&& fn)
{
    for (auto& [addr, nbr] : neighbors)
        fn(nbr);
}

template <typename F>
void NeighborTable::forEachNeighbor(F&& fn) const
{
    for (const auto& [addr, nbr] : neighbors)
        fn(nbr);
}

template <typename F>
void NeighborTable::forEachSession(F&& fn)
{
    for (auto& [addr, nbr] : peers)
        if (nbr->session)
            fn(*nbr->session);
}

template <typename F>
void NeighborTable::forEachSession(F&& fn) const
{
    for (const auto& [addr, nbr] : peers)
        if (nbr->session)
            fn(*nbr->session);
}
} // namespace routing

#endif // BGP_NEIGHBOR_TABLE_H
