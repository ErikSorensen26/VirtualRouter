/**
 * @file RoutingTable.h
 * @brief OSPF-internal RIB that selects the best path per prefix and drives global RIB installation.
 */

#ifndef OSPF_ROUTING_TABLE_H
#define OSPF_ROUTING_TABLE_H

#include <cstdint>
#include <IPAddress.h>
#include <unordered_set>
#include "TopologyTypes.hpp"

namespace core { class RoutingTable; }

namespace routing::ospf
{
class OspfProcess;
class Area;

/**
 * @brief OSPF-internal routing table that selects the best path per prefix and synchronises changes to the global RIB.
 * @ingroup OSPF_TOPOLOGY
 *
 * `OspfRib` maintains the full set of OSPF candidate paths (intra-area,
 * inter-area, and external) for every known prefix. When the set of candidates
 * for a prefix changes it recomputes the winner according to RFC 2328 §16
 * preference rules (intra > inter > external E1 > external E2) and propagates
 * additions and withdrawals to the global `core::RoutingTable`.
 *
 * Callers supply batched path updates via `replaceArea` or `replaceExternal`
 * and receive back a list of @ref OspfRouteChange events that describe exactly
 * which prefixes changed. Those events are used by the ABR summary originator
 * to decide whether to re-originate Type-3 LSAs.
 *
 * Area range suppression is handled here: when a range covers an intra-area
 * prefix the prefix is marked suppressed and a discard (null-route) entry is
 * installed in the global RIB at the range boundary instead.
 *
 * ## Architectural Role
 * Sits between the route derivation functions in `routemanager::` and the
 * global `core::RoutingTable`. It does not interpret LSA bodies — that is the
 * job of `RouteManager`. It also does not flood LSAs.
 *
 * ## Lifecycle & Ownership
 * Owned exclusively by `OspfProcess`. All methods must be called from the
 * owning process's scheduler thread.
 *
 * @warning Candidate paths are keyed by area and type; passing the wrong `Area`
 * reference to `replaceArea` will silently corrupt the area index and leave
 * stale entries in the global RIB.
 *
 * @see routemanager, TopologyTable, OspfProcess
 */
class OspfRib
{
    struct OspfDiscardKey;
public:
    /**
     * @brief Constructs the OSPF RIB and binds it to the owning process.
     *
     * Obtains a reference to the global `core::RoutingTable` from the process.
     * No routes are installed at construction time.
     *
     * @param process The OSPF process instance that owns this RIB.
     */
    OspfRib(OspfProcess& process);

    /**
     * @brief Returns the installed OSPF route for a prefix, or null if not present.
     *
     * @param prefix Exact prefix to look up.
     */
    const OspfRoute* lookup(const types::IPPrefix& prefix) const;

    /**
     * @brief Performs a longest-prefix-match for an address within a given area.
     *
     * Used by inter-area summary eligibility checks to determine whether a more
     * specific intra-area route already covers the candidate summary prefix.
     *
     * @param addr Address to match.
     * @param area Area whose intra-area routes to consider.
     * @return True if a matching route exists.
     */
    bool lpmLookup(const types::IPAddress& addr, uint32_t area) const;

    /**
     * @brief Atomically replaces all intra-area or inter-area paths for an area with a new set.
     *
     * Computes the symmetric difference between the old and new sets, recomputes
     * winners for all touched prefixes, and propagates changes to the global RIB.
     *
     * @param area  The area whose paths are being replaced.
     * @param paths New complete set of (prefix, path) pairs for the area.
     * @return List of route changes that occurred as a result.
     */
    std::vector<OspfRouteChange> replaceArea(Area& area, const std::vector<std::pair<types::IPPrefix, OspfPath>>& paths);

    /**
     * @brief Adds or withdraws a single intra-area or inter-area path for a prefix.
     *
     * If `path` is `nullopt` the existing path contributed by `area` for `prefix`
     * is removed. Otherwise it is replaced with the new value.
     *
     * @param area Area contributing the path.
     * @param path (prefix, optional path) pair; nullopt path means removal.
     * @return List of route changes (at most one entry for the affected prefix).
     */
    std::vector<OspfRouteChange> replaceRoute(Area& area, const std::pair<types::IPPrefix, std::optional<OspfPath>>& path);

    /**
     * @brief Atomically replaces the full set of external (Type-5) paths.
     *
     * Mirrors `replaceArea` but operates on the process-wide external path set
     * rather than a per-area intra/inter set.
     *
     * @param paths New set of external (prefix, path) pairs.
     */
    void replaceExternals(const std::vector<std::pair<types::IPPrefix, OspfPath>>& paths);

    /**
     * @brief Adds or withdraws a single external path.
     *
     * @param path (prefix, optional path) pair; nullopt path means removal.
     */
    void replaceExternal(const std::pair<types::IPPrefix, std::optional<OspfPath>>& path);

    /**
     * @brief Installs a discard (null-route) entry for an area range boundary.
     *
     * Area ranges aggregate intra-area prefixes behind a summary. A discard
     * route at the summary boundary prevents traffic from leaking to a less
     * specific default when the summary is not reachable.
     *
     * @param key  Composite key identifying the range and its source area.
     * @param cost Path cost to advertise for the discard entry.
     * @param ad   Administrative distance for the discard entry.
     */
    void installDiscardRoute(const OspfDiscardKey& key, uint32_t cost, uint8_t ad);

    /**
     * @brief Removes a previously installed discard route.
     *
     * @param key The same composite key used when installing.
     */
    void withdrawDiscardRoute(const OspfDiscardKey& key);

    /**
     * @brief Returns true if a prefix is eligible to be summarized as an inter-area summary.
     *
     * A prefix is ineligible if it is already installed as an inter-area or external
     * route (advertising it as a summary would create a routing loop).
     *
     * @param prefix Candidate summary prefix.
     */
    bool validateInterAreaSummaryEligibility(const types::IPPrefix& prefix) const;

    /**
     * @brief Recomputes range-based suppression for all intra-area prefixes in an area.
     *
     * Called when an area's range configuration changes. Prefixes that fall within
     * a configured range are marked suppressed; those outside are unsuppressed.
     *
     * @param areaId Area whose ranges changed.
     * @param ranges New set of configured area ranges.
     * @return Route change events for all prefixes whose suppression state changed.
     */
    std::vector<OspfRouteChange> refreshIntraRangeSuppression(uint32_t areaId, const std::unordered_set<types::IPPrefix>& ranges);

    /**
     * @brief Returns all unsuppressed intra-area routes contributed by the given area.
     *
     * Used by the ABR originator to build the Type-3 summary LSA set.
     *
     * @param area Area to query.
     */
    std::vector<std::pair<types::IPPrefix, OspfPath>> getIntraAreaRoutes(uint32_t area);

private:
    /// All candidate paths and the currently installed winner for one prefix.
    struct PrefixState
    {
        std::vector<OspfPath> canidates; ///< All competing paths; sorted by preference after each update.
        OspfRoute selected;              ///< The currently installed best-path route.
        bool hasSelected{false};         ///< False until the first winning path is installed.
    };

    /// Returns true if the given prefix already has an entry in the global RIB from this process.
    bool globalRibContains(const types::IPPrefix& prefix) const;

    std::unordered_map<types::IPPrefix, PrefixState> prefixStates; ///< All candidate paths per prefix.
    std::unordered_map<uint32_t, std::unordered_set<types::IPPrefix>> areaIndex; ///< Prefixes contributed by each area; used to diff on area replace.
    std::unordered_set<types::IPPrefix> processWide; ///< Prefixes contributed by process-wide external paths.

    /// Composite key for a discard route: the covered prefix and its optional source area.
    struct OspfDiscardKey
    {
        types::IPPrefix prefix;
        std::optional<uint32_t> areaId; ///< nullopt for process-wide discard routes.

        bool operator==(const OspfDiscardKey& o) const
        {
            return prefix == o.prefix && areaId == o.areaId;
        }
    };

    struct OspfDiscardKeyHash
    {
        size_t operator()(const OspfDiscardKey& k) const noexcept
        {
            size_t h = std::hash<types::IPPrefix>{}(k.prefix);
            if (k.areaId)
                h ^= std::hash<uint32_t>{}(*k.areaId) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<OspfDiscardKey, types::IPPrefix, OspfDiscardKeyHash> discardRoutes; ///< Active discard routes keyed by their range boundary.

    OspfProcess& process;       ///< Owning OSPF process; provides process ID and config access.
    core::RoutingTable& rib;    ///< Global RIB where selected routes are installed.

private:
    /// Accumulates intra/inter change events while recomputing winners for a batch of prefixes.
    struct RecomputeCtx
    {
        const uint32_t areaId;
        const std::unordered_set<types::IPPrefix>& ranges; ///< Active area ranges for suppression check.
        OspfRouteChange intraChange{};
        OspfRouteChange interChange{};
        bool intraChanged{false};
        bool interChanged{false};
    };

    /**
     * @brief Recomputes the best path for a single prefix and updates the global RIB.
     *
     * @param prefix   Prefix to recompute.
     * @param af       Address family (used when building the RIB entry).
     * @param procId   OSPF process ID used to tag the global RIB entry.
     * @param intraCtx Optional context to collect intra/inter change events; null for external recompute.
     * @return True if the result is an intra-area path, false if inter-area.
     */
    bool recomputeLocked(const types::IPPrefix& prefix, types::AddressFamily af, uint32_t procId, RecomputeCtx* intraCtx = nullptr);

    /**
     * @brief Recomputes best paths for a set of prefixes under an area's range suppression rules.
     *
     * @param touched  Prefixes whose candidate sets changed.
     * @param areaId   Area driving the recompute (used for suppression checks).
     * @param ranges   Area's current range configuration.
     * @return Aggregated list of route change events.
     */
    std::vector<OspfRouteChange> recomputeLocked(const std::unordered_set<types::IPPrefix>& touched, uint32_t areaId, const std::unordered_set<types::IPPrefix>& ranges);

    /**
     * @brief Recomputes best paths for a set of prefixes without area range suppression context.
     *
     * Used for external route recomputation where area ranges do not apply.
     *
     * @param touched Prefixes whose candidate sets changed.
     */
    void recomputeLocked(const std::unordered_set<types::IPPrefix>& touched);
};

} // namespace routing

#endif // OSPF_ROUTING_TABLE_H

