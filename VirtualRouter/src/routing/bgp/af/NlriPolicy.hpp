/**
 * @file NlriPolicy.hpp
 * @brief Abstract base class template for per-AFI/SAFI NLRI installation policy.
 * @ingroup BGP_AF
 */

#ifndef BGP_NLRI_POLICY_HPP
#define BGP_NLRI_POLICY_HPP

#include "bgp/rib/RibTypes.hpp"

namespace core { class VirtualRouter; }

namespace routing::bgp
{
enum class LocRibType;
template <typename N, LocRibType T>
class LocRib;
class BgpScope;

/**
 * @brief Abstract CRTP-style base that a concrete NLRI policy must inherit from.
 *
 * `NlriPolicy` bundles together the three compile-time knobs that describe an
 * address family (the NLRI type `N`, the Loc-RIB storage strategy `LR`, and the
 * AFI/SAFI constant `A`) with the runtime interface through which
 * `AddressFamilyInstance` installs and withdraws routes in the global RIB.
 *
 * ## Architectural Role
 * Concrete subclasses (e.g. `ExampleNlri`) inherit `NlriPolicy` and override the
 * four pure-virtual methods to perform address-family-specific translation from
 * BGP `LocalRoute` objects into `core::RibEntry` records that the platform RIB
 * understands.  `AddressFamilyInstance<N>` owns a single `N` instance and calls
 * these methods after best-path selection.
 *
 * ## Lifecycle & Ownership
 * Owned (by value) inside `AddressFamilyInstance<N>`.  Constructed once when the
 * address family is enabled on the `BgpScope`.  Not copyable or movable.
 *
 * ## Concurrency Model
 * All calls arrive on the BGP scope's scheduler thread.  Implementations must not
 * block or spawn threads.
 *
 * @tparam N   The NLRI prefix type (e.g. `types::IPv4Prefix`).
 * @tparam LR  The `LocRibType` enum value that selects the Loc-RIB data structure.
 * @tparam A   The `AfiSafi` constant that identifies this address family.
 *
 * @see AddressFamilyInstance
 * @ingroup BGP_AF
 */
template <typename N, LocRibType LR, AfiSafi A>
class NlriPolicy
{
public:
    /**
     * @brief Constructs the policy with references to the owning VRF and BGP scope.
     * @param v The VRF / platform router that provides the global RIB.
     * @param s The `BgpScope` that owns this address-family instance.
     */
    NlriPolicy(core::VirtualRouter& v, BgpScope& s)
        : vrf(v), scope(s) {}

    using LocRib = LocRib<N, LR>; ///< Loc-RIB type selected by the `LR` template parameter.
    using Nlri   = N;              ///< The NLRI prefix type for this address family.
    static constexpr AfiSafi afi = A; ///< The AFI/SAFI constant for this address family.

    /**
     * @brief Parameters passed to `installRoute` / `installRoutes` by `AddressFamilyInstance`.
     *
     * The concrete policy uses this bundle to build a platform `RibEntry` and
     * submit it to the global routing table.
     */
    struct NlriInstall
    {
        bgp::LocalRoute<N>& route;  ///< Best-path result including multipaths.
        bgp::PathAttribute  attrs;  ///< Resolved path attributes for the best route.
        uint64_t            metric; ///< Metric to program into the RIB entry.
        uint8_t             distance; ///< Administrative distance for this route.
        bool                recursiveHost; ///< When `false`, skip /32 or /128 next-hop routes.
    };

    /**
     * @brief Install a single best-path route into the global RIB.
     * @param install  Fully resolved installation parameters.
     */
    virtual void installRoute(const NlriInstall& install) = 0;

    /**
     * @brief Withdraw a single prefix from the global RIB.
     * @param nlri  The prefix to remove.
     */
    virtual void withdrawRoute(const N& nlri) = 0;

    /**
     * @brief Batch-install multiple routes into the global RIB.
     * @param install  Vector of resolved installation parameters, one per prefix.
     */
    virtual void installRoutes(const std::vector<NlriInstall>& install) = 0;

    /**
     * @brief Batch-withdraw multiple prefixes from the global RIB.
     * @param nlri  Vector of prefixes to remove.
     */
    virtual void withdrawRoutes(const std::vector<N>& nlri) = 0;

protected:
    core::VirtualRouter& vrf;     ///< The VRF / platform router (provides RIB access).
    BgpScope&          scope;   ///< The owning BGP scope (provides AS number, etc.).
};
} // namespace routing

#endif // BGP_NLRI_POLICY_HPP

