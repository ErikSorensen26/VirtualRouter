/**
 * @file TopologyTable.h
 * @brief OSPF topology table tracking the best reachability path to every known OSPF router.
 */

/**
 * @defgroup OSPF_TOPOLOGY OSPF Topology
 * @ingroup OSPF
 * @brief OSPF topology and route tables, route manager, and topology types.
 */

#ifndef OSPF_TOPOLOGY_TABLE_H
#define OSPF_TOPOLOGY_TABLE_H

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "TopologyTypes.hpp"

namespace routing::ospf
{
struct SpfResult;
struct OspfNextHop;
struct SpfNode;

/**
 * @brief Tracks the winning reachability entry for every OSPF router across all areas.
 * @ingroup OSPF_TOPOLOGY
 *
 * After each SPF run the `SpfManager` feeds the settled router vertices into this
 * table. `TopologyTable` merges intra-area and inter-area candidates for the same
 * Router ID and keeps only the best, following the RFC 2328 §16.1 preference
 * (intra-area beats inter-area).
 *
 * The primary consumer is external route computation: before a Type-5 or Type-7
 * LSA can be translated into a forwarding entry the computing router must resolve
 * the advertising ASBR's cost and next-hops through this table.
 *
 * ## Architectural Role
 * Sits between the SPF engine and the external route installation logic.
 * It does not install routes into the global RIB itself; it only provides
 * router reachability data that the route manager uses to resolve ASBR next-hops.
 *
 * ## Lifecycle & Ownership
 * Owned by `OspfProcess`. Updated after every SPF computation via
 * `consumeSpfResult` and `updateAreaAsbrs`. Cleared when the process resets.
 *
 * @see OspfRib, SpfEngine
 */
class TopologyTable
{
public:
    /**
     * @brief Merges the router vertices from a completed SPF run into the table.
     *
     * Iterates the settled routers in `result` and calls `mergeCanidates` for
     * the given area. Returns true if any reachability entry changed, which
     * signals the caller that external routes may need recomputation.
     *
     * @param area   Area identifier the SPF was computed for.
     * @param result Output of the SPF engine for that area.
     * @return True if at least one router's best reachability entry changed.
     */
    bool consumeSpfResult(uint32_t area, const SpfResult& result);

    /**
     * @brief Replaces all ASBR reachability entries for an area in one operation.
     *
     * Used when an inter-area summary (Type-4 LSA) arrives and the complete set
     * of ASBRs reachable through that area must be updated atomically.
     *
     * @param area  Area from which the ASBR reachability was learned.
     * @param asbrs New set of router reachability records for that area.
     * @return True if any entry changed.
     */
    bool updateAreaAsbrs(uint32_t area, const std::vector<OspfRouter>& asbrs);

    /**
     * @brief Adds or removes a single ASBR reachability entry for an area.
     *
     * @param area   Area the ASBR is reachable through.
     * @param asbr   Reachability record for the ASBR.
     * @param remove True to remove the entry; false (default) to add or update it.
     * @return True if the best reachability for that RID changed as a result.
     */
    bool updateAreaAsbr(uint32_t area, const OspfRouter& asbr, bool remove = false);

    /**
     * @brief Returns the best reachability record for a router, or null if unknown.
     *
     * @param rid Router ID to look up.
     * @return Pointer to the winning `RouterReach` entry, or nullptr.
     */
    const RouterReach* lookup(uint32_t rid) const;

    /**
     * @brief Returns the path cost to a router, or UINT32_MAX if unreachable.
     *
     * @param rid Router ID to query.
     */
    uint32_t lookupDistance(uint32_t rid) const;

    /// Removes all entries; called when the OSPF process resets or shuts down.
    void clear();

private:
    /// Distinguishes whether a reachability candidate was learned intra-area or inter-area.
    enum class Type { INTRA, INTER };

    /// Key that uniquely identifies one candidate entry: the source type and the RID.
    struct ReachEntry
    {
        Type type;
        uint32_t rid;

        bool operator==(const ReachEntry& other) const noexcept
        {
            return type == other.type &&
                   rid == other.rid;
        }
    };

    /// Finisher-based hash for ReachEntry; mixes type and rid bits with a Murmur-style finalizer.
    struct ReachEntryHash
    {
        size_t operator()(const ReachEntry& k) const
        {
            uint64_t packed = (static_cast<uint64_t>(k.rid) << 1) | static_cast<uint64_t>(k.type);
            packed += 0x9e3779b97f4a7c15ull;
            packed = (packed ^ (packed >> 30)) * 0xbf58476d1ce4e5b9ull;
            packed = (packed ^ (packed >> 27)) * 0x94d049bb133111ebull;
            packed ^= (packed >> 31);

            return static_cast<size_t>(packed);
        }
    };

    /// Per-area set of (type, rid) pairs contributed to the merged `reach` table.
    std::unordered_map<uint32_t, std::unordered_set<ReachEntry, ReachEntryHash>> areaReach;
    /// Best merged reachability record per Router ID across all areas.
    std::unordered_map<uint32_t, RouterReach> reach;

private:
    /**
     * @brief Attempts to merge a single candidate into the best-path `reach` table.
     *
     * Returns `true` if the candidate was installed as the new best path,
     * `false` if an existing entry wins, or `nullopt` if the candidate was
     * removed and the best path must be recomputed from the remaining candidates.
     *
     * @param candidate Router reachability candidate to evaluate.
     * @return true = newly installed, false = existing best unchanged, nullopt = removed.
     */
    std::optional<bool> mergeCanidate(const OspfRouter& candidate);

    /**
     * @brief Merges a full set of candidates for one area and recomputes affected entries.
     *
     * @param area      Area identifier for attribution.
     * @param candidates New set of router reachability records for the area.
     * @param type      Whether candidates are intra-area or inter-area.
     * @return True if any best-path entry changed.
     */
    bool mergeCanidates(uint32_t area, const std::vector<OspfRouter>& candidates, Type type);
};

} // namespace routing

#endif // OSPF_TOPOLOGY_TABLE_H

