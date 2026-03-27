/**
 * @file LocRib.hpp
 * @brief Local RIB storage backends for best-path BGP routes.
 */

/**
 * @defgroup BGP_RIB BGP RIB
 * @ingroup BGP
 * @brief Local RIB storage and RIB type definitions for best-path BGP routes.
 */

#ifndef BGP_LOC_RIB_HPP
#define BGP_LOC_RIB_HPP

#include <unordered_map>
#include <LpcTrie.hpp>

#include "RibTypes.hpp"

namespace routing::bgp
{

/**
 * @brief Selects the backing data structure used by a LocRib specialization.
 * @ingroup BGP_RIB
 */
enum class LocRibType
{
    LPC_TRIE, ///< Longest-prefix-match trie; efficient for prefix lookups and range iteration.
    HASH_MAP  ///< Flat hash map; O(1) exact lookups, suitable for non-prefix NLRI types.
};

/**
 * @brief Primary template (incomplete): select a specialization via LocRibType.
 * @ingroup BGP_RIB
 *
 * AddressFamilyInstance selects the appropriate backend through the NLRI policy
 * type's `LocRib` typedef. All concrete storage and lookup operations live in
 * the partial specializations below.
 *
 * @tparam N  Prefix type (e.g. types::IPv4Prefix). Must match the NLRI type of
 *             the owning AddressFamilyInstance.
 * @tparam T  Storage backend selector from @ref LocRibType.
 */
template <typename N, LocRibType T>
class LocRib;

/**
 * @brief Loc-RIB backed by a Longest-Prefix-Match trie.
 * @ingroup BGP_RIB
 *
 * Intended for IP-prefix NLRI types (IPv4, IPv6) where longest-prefix lookups
 * and ordered prefix iteration are valuable. The underlying LPCTrie provides
 * O(prefix-length) exact and longest-match lookups.
 *
 * ## Architectural Role
 * Stores the single best @ref LocalRoute for each prefix after the decision
 * engine has run. Pointers into this table are held by Adj-RIB-Out entries;
 * any erase must be coordinated with Adj-RIB-Out cleanup in
 * AddressFamilyInstance.
 *
 * @tparam N  Prefix type whose binary representation fits in `sizeof(N)` bytes.
 *
 * @warning All methods are private — this class is a friend of
 * AddressFamilyInstance, which is the sole caller.
 */
template <typename N>
class LocRib<N, LocRibType::LPC_TRIE>
{
    static constexpr LocRibType type = LocRibType::LPC_TRIE;

    /**
     * @brief Inserts or replaces the best route for a prefix.
     *
     * @param nlri          Raw prefix bytes (network order).
     * @param plen          Prefix length in bits.
     * @param inboundRoute  Best route selected by DecisionEngine; must remain
     *                      live in Adj-RIB-In for the lifetime of this entry.
     * @return True if a new entry was created; false if an existing one was replaced.
     */
    bool insert(const uint8_t* nlri, uint8_t plen, const InboundRouteBase& inboundRoute)
    {
        return locRib.insert(nlri, plen, &inboundRoute);
    }

    /**
     * @brief Removes the entry for a prefix.
     *
     * @param nlri  Raw prefix bytes.
     * @param plen  Prefix length in bits.
     * @return True if the entry was found and removed.
     */
    bool erase(const uint8_t* nlri, uint8_t plen)
    {
        return locRib.erase(nlri, plen);
    }

    /**
     * @brief Returns the LocalRoute for an exact prefix match.
     *
     * @param nlri  Raw prefix bytes.
     * @param plen  Prefix length in bits.
     * @return Reference to the stored LocalRoute.
     * @warning Behaviour is undefined if the prefix is not present.
     */
    const LocalRoute<N>& at(const uint8_t* nlri, uint8_t plen)
    {
        return locRib.lookupExact(nlri, plen);
    }

    /**
     * @brief Invokes a callback for every prefix-route pair in the trie.
     *
     * @tparam F  Callable with signature `void(const uint8_t*, uint8_t, const InboundRouteBase*)`.
     * @param f   Callback to invoke per entry.
     */
    template <typename F>
    void forEach(F&& f) const
    {
        locRib.forEach([f = std::forward<F>(f)](const uint8_t* prefix, uint8_t plen, const InboundRouteBase* route) {
            f(prefix, plen, route);
        });
    }

private:
    types::LPCTrie<sizeof(N), LocalRoute<N>> locRib; ///< Underlying LPC-trie storage.
};

/**
 * @brief Loc-RIB backed by an unordered hash map.
 * @ingroup BGP_RIB
 *
 * Suitable for NLRI types that are not IP prefixes (e.g. VPN labels, flow
 * specs) where longest-prefix matching is not needed and O(1) exact lookup
 * is preferred.
 *
 * ## Architectural Role
 * Same contract as the LPC_TRIE specialization: stores one @ref LocalRoute
 * per NLRI, updated exclusively by AddressFamilyInstance after each
 * decision-engine run.
 *
 * @tparam N  NLRI type. Must be equality-comparable and hashable via std::hash<N>.
 *
 * @warning All methods are private — this class is a friend of
 * AddressFamilyInstance, which is the sole caller.
 */
template <typename N>
class LocRib<N, LocRibType::HASH_MAP>
{
    static constexpr LocRibType type = LocRibType::HASH_MAP;

    /**
     * @brief Inserts a new best route for an NLRI.
     *
     * @param nlri   The NLRI key.
     * @param route  Best route; must remain live in Adj-RIB-In.
     * @return True if inserted; false if the key already exists.
     */
    bool insert(const N& nlri, const InboundRouteBase& route)
    {
        auto [_, ok] = locRib.emplace(nlri, &route);
        return ok;
    }

    /**
     * @brief Removes the entry for an NLRI.
     * @param nlri  Key to erase.
     */
    bool erase(const N& nlri)
    {
        locRib.erase(nlri);
    }

    /**
     * @brief Invokes a callback for every NLRI-route pair.
     *
     * @tparam F  Callable with signature `void(const N&, const LocalRoute<N>&)`.
     * @param f   Callback to invoke per entry.
     */
    template <typename F>
    void forEach(F&& f) const
    {
        for (const auto& [n, rt] : locRib)
            f(n, rt);
    }

private:
    std::unordered_map<N, LocalRoute<N>> locRib; ///< Hash-map storage keyed by NLRI.
};

} // namespace routing::bgp

#endif // BGP_LOC_RIB_HPP

