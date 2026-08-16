/**
 * @file Neighbor.h
 * @brief Represents one configured BGP neighbor (peer).
 */

/**
 * @defgroup BGP_NEIGHBOR BGP Neighbors
 * @ingroup BGP
 * @brief Neighbor, neighbor-AF, peer templates, configs, and neighbor table.
 */

#ifndef BGP_NEIGHBOR_H
#define BGP_NEIGHBOR_H

#include <bitset>
#include <optional>
#include <string>
#include <ControlScheduler.h>

#include "bgp/BgpTypes.hpp"
#include "NeighborConfigs.hpp"
#include "NeighborAf.h"

class Internal_BgpTest;

namespace routing::bgp
{
class BgpScope;
class Session;
class NeighborAf;

/**
 * @brief Represents one configured (or dynamically-created) BGP neighbor.
 *
 * A Neighbor aggregates:
 *   - The peer IP address and (once established) the peer's BGP Router ID.
 *   - A non-owning pointer to the live Session (null when not established).
 *   - Per-AF state objects (@ref NeighborAf), one per activated address family.
 *   - Session-level configuration with optional peer-group/template inheritance (@ref NeighborConfigs).
 *   - Attribute-type bitmasks used during UPDATE parsing.
 *
 * Neighbor objects are owned by @ref NeighborTable.
 *
 * @ingroup BGP_NEIGHBOR
 */
class Neighbor
{
public:
    /**
     * @brief Construct a neighbor for the given IP address within the given scope.
     * @ingroup BGP_NEIGHBOR
     * @param ipAddress The peer IP address.
     * @param scope     The BgpScope that owns this neighbor.
     */
    Neighbor(config::BgpNeighborSessionRegistry& cfgs, const types::IPAddress& ipAddress, NeighborTable& ntable, core::ProcessQueue& schdlr);

    /**
     * @brief Construct a dynamic neighbor for the given IP address within the given scope.
     * @ingroup BGP_NEIGHBOR
     * @param ipAddress The peer IP address.
     * @param scope     The BgpScope that owns this neighbor.
     */
    Neighbor(PeerGroup& dynCfgs, const types::IPAddress& ipAddress, NeighborTable& ntable, core::ProcessQueue& schdlr);

    /**
     * @brief Destructor. Cleans up all per-AF state.
     */
    ~Neighbor();

    void enqueueConnectionRestart();
    void enqueueSyncShutdown(bool shutdown);
    void enqueueBuildAttributeRanges();
    void enqueueSyncRemoteAs(std::optional<uint32_t> remoteAs);
    void syncClassification() { syncEbgp(); }
    void enqueueMarkAllOutbound(OutAttr attr);
    void enqueueSyncPeerGroup(std::optional<std::string> name);
    void enqueueSyncPeerSessionTemplate(std::optional<std::string> name);

    const types::IPAddress neighborAddress; ///< IP address of this BGP peer.

    uint32_t getRouterId() const { return rid; }

    const NeighborConfigs& getConfigs() const noexcept { return configs; }

    BgpScope& getScope() const noexcept;

    /**
     * @brief Retrieve the NeighborAf for the given AFI/SAFI (const overload).
     * @param afi The address family to look up.
     * @return Const reference to the NeighborAf.
     */
    NeighborAf& getAfNeighbor(const AfiSafi& afi);

    /**
     * @brief Retrieve the NeighborAf for the given AFI/SAFI, if activated.
     * @param afi The address family to look up.
     * @return Pointer to the NeighborAf, or `nullptr` if not activated for this neighbor.
     */
    NeighborAf* findAfNeighbor(const AfiSafi& afi);

    /**
     * @brief Invoke a callable for every activated per-AF neighbor state object (const overload).
     * @tparam F Callable type accepting a @c const NeighborAf& parameter.
     * @param fn The callable to invoke.
     */
    template <typename F>
    void forEachAfNeighbor(F&& fn) const;

    /**
     * @brief Invoke a callable for every activated per-AF neighbor state object.
     * @tparam F Callable type accepting a @c NeighborAf& parameter.
     * @param fn The callable to invoke.
     */
    template <typename F>
    void forEachAfNeighbor(F&& fn);

    /**
     * @brief Return true when this peer is in a different AS (external BGP).
     */
    bool isEbgp() const noexcept;

    /**
     * @brief Return true when this peer is a confederation eBGP peer.
     */
    bool isConfedEbgp() const noexcept;

    /**
     * @brief Returns the dynamic peer group this neighbor was created from, if any.
     *
     * Non-null only for neighbors spawned by @ref NeighborTable::createDynamicNeighbor
     * from a BGP_LISTEN_RANGE match. Such neighbors already inherit their session and
     * policy config from the matched group at creation, so @ref PeerTemplateTable's
     * sync methods skip re-resolving PEER_GROUP / INHERIT_PEER_SESSION /
     * INHERIT_PEER_POLICY for them — those fields are not configurable per-neighbor
     * on a dynamic neighbor.
     *
     * @return Pointer to the owning `PeerGroup`, or `nullptr` for a statically configured neighbor.
     */
    const PeerGroup* getDynamic() const noexcept;

    /**
     * @brief Returns the cached discard/withdraw attribute-type bitmasks.
     */
    const auto& getAttrRanges() const noexcept { return attrRanges; }

private:
    friend ::Internal_BgpTest;
    friend NeighborAf;
    friend NeighborTable;
    friend PeerTemplateTable;
    friend Session;

    uint32_t rid = 0; ///< Peer's BGP Router ID (network-byte-order); 0 until OPEN is received.

    Session* session = nullptr; ///< Non-owning pointer to the active Session; null when not established.

    // SYNC

    /**
     * @brief Recomputes the eBGP / confederation-eBGP classification from current config.
     *
     * A peer is eBGP when its remote AS differs from ours and it is not a configured
     * confederation peer. Must run whenever REMOTE_AS, the local AS, or the
     * confederation peer list changes — driven by `enqueueSyncRemoteAs` and
     * `BgpScope::enqueueSyncConfederation`.
     */
    void syncEbgp();

    /**
     * @brief Same as `syncEbgp()`, using an already-known REMOTE_AS value.
     *
     * Lets the REMOTE_AS config-change entry point avoid re-reading the field
     * it was just handed.
     *
     * @param remoteAs The new `REMOTE_AS` value.
     */
    void syncEbgp(std::optional<uint32_t> remoteAs);

    // HELPERS

    /**
     * @brief Activate the given address family for this neighbor, creating a NeighborAf entry.
     * @param afi The AFI/SAFI to activate.
     */
    void addAfNeighbor(const AfiSafi& afi);

    /**
     * @brief Deactivate the given address family, removing the NeighborAf entry.
     * @param afi The AFI/SAFI to deactivate.
     */
    void delAfNeighbor(AfiSafi& afi);

    /**
     * @brief Rebuild the @ref AttributeRanges from the current neighbor config.
     *
     * Must be called whenever the ATTRIBUTE_DISCARD or ATTRIBUTE_WITHDRAW config changes.
     */
    void buildAttributeRanges();

    /**
     * @brief Clears the SHUTDOWN state and lets the neighbor table re-establish this session.
     */
    void unshutdown();

    /**
     * @brief Finishes constructing this neighbor: binds config context, activates
     *        configured address families, and resolves inherited templates.
     *
     * Must be called once, immediately after construction, before the neighbor is
     * usable. Performs, in order:
     *  - Binds this neighbor as the context for its session config and BGP_BASE,
     *    so registry appliers can reach it through the Neighbor pointer.
     *  - Activates the address families enabled for this neighbor's VRF via AF_VRF.
     *  - Resolves the session-level peer template from INHERIT_PEER_SESSION.
     */
    void initialize();

    struct AttributeRanges
    {
        std::bitset<256> discard;  ///< Attribute types to silently discard on receipt.
        std::bitset<256> withdraw; ///< Attribute types that cause route withdrawal on receipt.
    } attrRanges; ///< Cached discard/withdraw bitmasks built from config.
    
    NeighborTable& ntable;
    core::ProcessQueue scheduler;
    NeighborConfigs configs;

    struct Private
    {
        private:
        friend Neighbor;
        std::atomic<bool> isEbgp{false};
        std::atomic<bool> inConfed{false};
        std::unordered_map<AfiSafi, NeighborAf> afNeighbors; ///< Per-AF state, keyed by AfiSafi.
    } priv;
};

template <typename F>
void Neighbor::forEachAfNeighbor(F&& fn) const
{
    for (const auto& [_, nbr] : priv.afNeighbors)
        fn(nbr);
}

template <typename F>
void Neighbor::forEachAfNeighbor(F&& fn)
{
    for (auto& [_, nbr] : priv.afNeighbors)
        fn(nbr);
}
} // namespace routing

#endif // BGP_NEIGHBOR_H

