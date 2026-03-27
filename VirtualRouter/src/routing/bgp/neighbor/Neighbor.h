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
    Neighbor(const types::IPAddress& ipAddress, BgpProcess& proc);

    /**
     * @brief Destructor. Cleans up all per-AF state.
     */
    ~Neighbor();

    const types::IPAddress neighborAddress; ///< IP address of this BGP peer.

    uint32_t rid = 0; ///< Peer's BGP Router ID (network-byte-order); 0 until OPEN is received.

    Session* session = nullptr; ///< Non-owning pointer to the active Session; null when not established.

    // True for neighbors created dynamically via bgp listen range.
    // Dynamic neighbors are passive-only and not owned by the static config.
    bool dynamic = false; ///< True when this neighbor was created by a "bgp listen range" match.

    BgpProcess& getProcess() { return process; }
    const BgpProcess& getProcess() const { return process; }

    /**
     * @brief Return true when this peer is in a different AS (external BGP).
     */
    bool isEbgp() const noexcept;

    /**
     * @brief Return true when this peer is a confederation eBGP peer.
     */
    bool isConfedEbgp() const noexcept;

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
     * @brief Retrieve the NeighborAf for the given AFI/SAFI.
     * @param afi The address family to look up.
     * @return Reference to the NeighborAf.
     */
    NeighborAf& getAfNeighbor(const AfiSafi& afi);

    /**
     * @brief Retrieve the NeighborAf for the given AFI/SAFI (const overload).
     * @param afi The address family to look up.
     * @return Const reference to the NeighborAf.
     */
    const NeighborAf& getAfNeighbor(const AfiSafi& afi) const;

    /**
     * @brief Invoke a callable for every activated per-AF neighbor state object.
     * @tparam F Callable type accepting a @c NeighborAf& parameter.
     * @param fn The callable to invoke.
     */
    template <typename F>
    void forEachAfNeighbor(F&& fn)
    {
        for (auto& [_, nbr] : afNeighbors)
            fn(nbr);
    }

    /**
     * @brief Invoke a callable for every activated per-AF neighbor state object (const overload).
     * @tparam F Callable type accepting a @c const NeighborAf& parameter.
     * @param fn The callable to invoke.
     */
    template <typename F>
    void forEachAfNeighbor(F&& fn) const
    {
        for (const auto& [_, nbr] : afNeighbors)
            fn(nbr);
    }

    NeighborConfigs& getConfigs() { return configs; }
    const NeighborConfigs& getConfigs() const { return configs; }
    core::ProcessQueueRef& getScheduler() { return scheduler; }
    const core::ProcessQueueRef& getScheduler() const { return scheduler; }

    /**
     * @brief Bitmasks describing per-attribute-type behavior during UPDATE parsing.
     *
     * Built from the ATTRIBUTE_DISCARD / ATTRIBUTE_WITHDRAW config fields and cached
     * here to avoid repeated config lookups in the hot path.
     */
    struct AttributeRanges
    {
        std::bitset<256> discard;  ///< Attribute types to silently discard on receipt.
        std::bitset<256> withdraw; ///< Attribute types that cause route withdrawal on receipt.
    };

    /**
     * @brief Rebuild the @ref AttributeRanges from the current neighbor config.
     *
     * Must be called whenever the ATTRIBUTE_DISCARD or ATTRIBUTE_WITHDRAW config changes.
     */
    void buildAttributeRanges();

    /**
     * @brief Return the cached attribute-range bitmasks.
     */
    const AttributeRanges& getAttrRanges() { return attrRanges; }

private:
    AttributeRanges attrRanges; ///< Cached discard/withdraw bitmasks built from config.

private:
    friend NeighborAf;

    BgpProcess& process;
    core::ProcessQueueRef scheduler;

    std::unordered_map<AfiSafi, NeighborAf> afNeighbors; ///< Per-AF state, keyed by AfiSafi.

    NeighborConfigs configs;
};
} // namespace routing

#endif // BGP_NEIGHBOR_H

