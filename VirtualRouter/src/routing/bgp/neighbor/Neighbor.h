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
#include <ControlScheduler.h>

#include "bgp/BgpTypes.hpp"
#include "NeighborConfigs.hpp"
#include "NeighborAf.h"

class Internal_BgpTest;

namespace routing::bgp
{
class BgpProcess;
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
     * @brief Construct a neighbor for the given IP address within the given process.
     * @ingroup BGP_NEIGHBOR
     * @param ipAddress The peer IP address.
     * @param proc      The BgpProcess that owns this neighbor.
     */
    Neighbor(const types::IPAddress& ipAddress, NeighborTable& ntable, core::ProcessQueue& schdlr);

    /**
     * @brief Destructor. Cleans up all per-AF state.
     */
    ~Neighbor();

    void enqueueConnectionRestart();

    const types::IPAddress neighborAddress; ///< IP address of this BGP peer.

    uint32_t getRouterId() const { return rid; }

    const NeighborConfigs& getConfigs() const noexcept { return configs; }

    /**
     * @brief Retrieve the NeighborAf for the given AFI/SAFI (const overload).
     * @param afi The address family to look up.
     * @return Const reference to the NeighborAf.
     */
    NeighborAf& getAfNeighbor(const AfiSafi& afi);

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

    bool dynamic = false; ///< True when this neighbor was created by a "bgp listen range" match.

    // SYNC

    /**
     * TODO add doxy comment
     * TODO run this any time remote-as or global confederations change
     */
    void syncEbgp();

    // HELPERS

    /**
     * @brief Activate the given address family for this neighbor, creating a NeighborAf entry.
     * @param afi The AFI/SAFI to activate.
     */
    void addAfNeighbor(AfiSafi& afi);

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
     * TODO add doxy comment
     */
    void unshutdown();

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

