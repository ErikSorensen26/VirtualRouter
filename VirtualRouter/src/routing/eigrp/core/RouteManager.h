/**
 * @file RouteManager.h
 * @brief Translates EIGRP topology successors into global RIB entries.
 */

#ifndef EIGRP_ROUTE_MANAGER_H
#define EIGRP_ROUTE_MANAGER_H

#include <vector>
#include <cstdint>
#include <IPAddress.h>

#include "routing/RoutingTable.hpp"
#include "eigrp/core/EigrpConfig.h"
#include "eigrp/topology/TopologyTable.h"

namespace routing::eigrp
{
class Eigrp;
struct RouteInfo;
class EigrpInterface;

/**
 * @brief Synchronizes the global RIB with the current EIGRP topology table.
 * @ingroup EIGRP_CORE
 *
 * `RouteManager` is the single point responsible for translating EIGRP
 * topology entries (with their chosen successors) into `RibEntry` objects and
 * installing or withdrawing them from the VRF routing table.  It is also
 * responsible for applying the RIB scale factor (`rib-scale`) so that EIGRP
 * metrics map to a comparable RIB metric range.
 *
 * ## Architectural Role
 * Called by the @ref DuelEngine after any DUAL convergence event that changes
 * the successor set for one or more prefixes.  It does not participate in
 * DUAL itself; it is purely an output stage that translates EIGRP-internal
 * data structures into the VRF RIB API.
 *
 * ## Lifecycle & Ownership
 * Owned by and lives inside the @ref Eigrp object.  The `rib` reference is
 * obtained from the VRF at construction and must remain valid for the
 * lifetime of this object.
 *
 * @see TopologyEntry
 * @see DuelEngine
 */
class RouteManager
{
public:
    /**
     * @brief Constructs the route manager bound to the given EIGRP process.
     *
     * @param process The @ref Eigrp process whose topology results are to be
     *                installed into the RIB.
     */
    explicit RouteManager(Eigrp& process);

    /**
     * @brief Removes the RIB entry for the given prefix, if present.
     *
     * Called when a prefix has no remaining successors (all routes are
     * unreachable or the entry has been pruned).
     *
     * @param withdraws Prefix to remove from the RIB.
     */
    void withdrawRoute(const types::IPPrefix withdraws);

    /**
     * @brief Synchronizes a batch of topology entries with the RIB.
     *
     * For each entry in `entry`, installs or withdraws the corresponding RIB
     * route based on the current successor list.
     *
     * @param entry Topology entries whose RIB state must be updated.
     */
    void synchronizeRoutes(const std::vector<TopologyEntry*>& entry);

    /**
     * @brief Synchronizes a single topology entry with the RIB.
     *
     * Installs the entry's successors as ECMP next-hops if any exist;
     * otherwise withdraws the prefix from the RIB.
     *
     * @param entry Topology entry to synchronize.
     */
    void synchronizeRoute(const TopologyEntry& entry);

private:

    Eigrp& base;           ///< Owning EIGRP process.
    core::RoutingTable& rib; ///< VRF routing table where RIB entries are installed.
    types::AddressFamily af; ///< Address family governing which RibEntry type to use.
    uint32_t as;           ///< AS number used as the RIB process identifier.

    /**
     * @brief Installs or withdraws the RIB entry for a single topology entry.
     *
     * Builds a `RibEntry<AddrType>` from the topology entry's successor list
     * and calls `rib.addRoute<AddrType>()`.  If the successor list is empty
     * the prefix is withdrawn instead.
     *
     * @tparam AddrType Address representation type for the RIB entry.
     *                  Must be `uint32_t` for IPv4 or `uint128_t` for IPv6.
     *
     * @param entryPtr Topology entry to process; returns nullptr if null.
     * @param scale    RIB-scale multiplier applied to the EIGRP feasible distance.
     * @return Pointer to the best @ref RouteInfo that was installed, or nullptr
     *         on withdrawal or failure.
     */
    template <typename AddrType>
    const RouteInfo* syncRoute(const TopologyEntry* entryPtr, uint8_t scale)
    {
        if (!entryPtr) return nullptr;
        std::optional<bool> isExternal{std::nullopt};
        auto& entry = *entryPtr;
        auto bestIt = entry.routesBySource.find(entry.bestNeighbor);
        if (entry.successors.empty() || bestIt == entry.routesBySource.end())
        {
            withdrawRoute(entry.prefix);
            return nullptr;
        }
        core::RibEntry<AddrType>* ribEntry = new core::RibEntry<AddrType>;

        for (const auto& neighbor : entry.successors)
        {
            auto it = entry.routesBySource.find(neighbor);
            if (it == entry.routesBySource.end()) continue;

            if (!isExternal.has_value())
            {
                isExternal = it->second.routeInfo.routeType == RouteType::EXTERNAL;
            }

            ribEntry->addNextHop(
                af == types::AddressFamily::IPv4 ? neighbor.v4() : neighbor.v6(),
                it->second.routeInfo.originInterface,
                1
            );
        }

        if constexpr (std::is_same_v<AddrType, uint32_t>)
        {
            ribEntry->prefix = bestIt->second.routeInfo.prefix.v4();
        }
        else
        {
            ribEntry->prefix = bestIt->second.routeInfo.prefix.v6();
        }

        ribEntry->length = bestIt->second.routeInfo.prefix.prefixLength;
        ribEntry->source = *isExternal ? core::RouteSource::EIGRP_EXTERNAL : core::RouteSource::EIGRP_INTERNAL;
        ribEntry->processId = as;
        ribEntry->adminDistance = bestIt->second.routeInfo.adminDistance;
        ribEntry->metric = bestIt->second.routeInfo.feasibleDistance * scale;

        rib.addRoute<AddrType>(ribEntry);
        return &bestIt->second;
    }
};
} // namespace routing::eigrp

#endif // EIGRP_ROUTE_MANAGER_H

