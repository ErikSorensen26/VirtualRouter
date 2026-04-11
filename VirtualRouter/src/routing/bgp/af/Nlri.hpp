/**
 * @file Nlri.hpp
 * @brief Concrete NLRI policy implementations and the active Nlri variant type.
 */

#ifndef BGP_NLRI_HPP
#define BGP_NLRI_HPP

#include <variant>
#include <IPAddress.h>
#include <LpcTrie.hpp>
#include <VirtualRouter.h>

#include "ProcessAccessor.h"
#include "NlriPolicy.hpp"
#include "bgp/BgpTypes.hpp"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/rib/LocRib.hpp"

namespace routing
{

/// Convenience alias for the NlriPolicy base of ExampleNlri (IPv4 unicast, LPC-trie backed).
using ExampleNlriType = bgp::NlriPolicy<types::IPv4Prefix, bgp::LocRibType::LPC_TRIE, bgp::AfiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST}>;

/**
 * @brief IPv4 unicast NLRI policy: encodes/decodes prefixes and installs routes into the VRF RIB.
 * @ingroup BGP_AF
 *
 * Implements the NLRI policy interface required by AddressFamilyInstance for the
 * IPv4 unicast address family. Responsibilities:
 * - Wire encoding and decoding of IPv4 prefixes (RFC 4271 §4.3).
 * - Single and batched installation of best-path routes into the VRF routing table.
 * - Single and batched withdrawal of routes from the VRF routing table.
 * - Recursive next-hop resolution: drops the route if the next-hop cannot be
 *   resolved, or if the resolved entry is a host route (/32) and
 *   BGP_RECURSIVE_HOST is disabled.
 *
 * ## Architectural Role
 * ExampleNlri is the glue between the AFI-agnostic AddressFamilyInstance and the
 * VRF's RoutingTable. It is constructed once per BGP process and VRF pair.
 * AddressFamilyInstance calls `installRoute`/`withdrawRoute` on the policy object;
 * ExampleNlri translates those calls into RoutingTable operations.
 *
 * ## Lifecycle & Ownership
 * Owned by AddressFamilyInstance<ExampleNlri> as data member `policy`. The `rib`
 * reference must remain valid for the lifetime of the policy, which is guaranteed
 * because the VRF outlives the BGP process.
 *
 * @see NlriPolicy, AddressFamilyInstance
 */
class ExampleNlri : public ExampleNlriType
{
public:
    /**
     * @brief Constructs the NLRI policy bound to a VRF and BGP process.
     * @ingroup BGP_AF
     *
     * @param vrf   VRF whose routing table receives installed routes.
     * @param proc  BGP process; used by the NlriPolicy base to access AS number and config.
     */
    ExampleNlri(core::VirtualRouter& vrf, bgp::BgpProcess& proc) : ExampleNlriType(vrf, proc),
        rib(vrf.getRib()) {}

    /**
     * @brief Installs a single best-path route into the VRF routing table.
     *
     * Builds a RibEntry from the LocalRoute, resolves the next-hop, and calls
     * RoutingTable::addRoute. If next-hop resolution fails the route is silently
     * dropped (the BGP Loc-RIB still holds it for re-evaluation on NHT change).
     *
     * @param install  Route descriptor produced by AddressFamilyInstance::buildInstall.
     */
    void installRoute(const NlriInstall& install) override
    {
        core::RibEntry<uint32_t>* route = buildRoute(install);
        if (!route) return;
        rib.addRoute(route);
    }

    /**
     * @brief Installs multiple best-path routes in a single batched RIB operation.
     *
     * Routes with unresolvable next-hops are skipped. The batch is forwarded to
     * RoutingTable::addRoutes to minimise per-prefix locking overhead.
     *
     * @param installs  Vector of route descriptors to install.
     */
    void installRoutes(const std::vector<NlriInstall>& installs) override
    {
        std::vector<core::RibEntry<uint32_t>*> entries;
        entries.reserve(installs.size());

        for (const auto& install : installs)
        {
            core::RibEntry<uint32_t>* route = buildRoute(install);
            if (!route) continue;
            entries.push_back(route);
        }

        if (entries.empty()) return;

        rib.addRoutes(entries);
    }

    /**
     * @brief Withdraws a single prefix from the VRF routing table.
     * @param nlri  Prefix to remove; identified by address and prefix length.
     */
    void withdrawRoute(const types::IPv4Prefix& nlri) override
    {
        rib.removeRoute(nlri.addr, nlri.prefixLength, core::RouteSource::BGP, bgp::ProcessAccessor::getAsNum(process));
    }

    /**
     * @brief Withdraws multiple prefixes in a single batched RIB operation.
     * @param nlri  Prefixes to remove.
     */
    void withdrawRoutes(const std::vector<Nlri>& nlri) override
    {
        std::vector<std::pair<uint32_t, uint8_t>> withdraws;
        withdraws.reserve(nlri.size());
        for (const auto& n : nlri)
            withdraws.push_back({n.addr, n.prefixLength});
        rib.removeRoutes(withdraws, core::RouteSource::BGP, bgp::ProcessAccessor::getAsNum(process));
    }

    /**
     * @brief Returns the number of bytes needed to encode an IPv4 prefix on the wire.
     *
     * Wire format: 1 byte for prefix length followed by the minimum number of
     * address bytes needed to represent the prefix (RFC 4271 §4.3).
     *
     * @param n  Prefix to measure.
     * @return   Encoded size in bytes.
     */
    static size_t nlriEncodedSize(const types::IPv4Prefix& n)
    {
        return 1u + (static_cast<size_t>(n.prefixLength) + 7u) / 8u;
    }

    /**
     * @brief Encodes an IPv4 prefix into a wire-format buffer.
     *
     * Writes the prefix-length byte followed by the significant prefix bytes.
     * The caller must ensure `buf` has at least `nlriEncodedSize(n)` bytes.
     *
     * @param buf  Destination buffer; must be at least nlriEncodedSize(n) bytes.
     * @param n    Prefix to encode.
     */
    static void encodeNlri(uint8_t* buf, const types::IPv4Prefix& n)
    {
        buf[0] = n.prefixLength;
        size_t bytes = (static_cast<size_t>(n.prefixLength) + 7u) / 8u;
        utils::writeBytes(buf + 1, n.addr, bytes);
    }

    /**
     * @brief Decodes an IPv4 prefix from a wire-format buffer.
     *
     * Reads the prefix-length byte and the corresponding address bytes.
     *
     * @param buf  Source buffer pointing at the prefix-length byte.
     * @param n    Output prefix populated on return.
     * @return     Number of bytes consumed from `buf`.
     */
    static size_t decodeNlri(const uint8_t* buf, types::IPv4Prefix& n)
    {
        n.prefixLength = buf[0];
        size_t bytes = (static_cast<size_t>(n.prefixLength) + 7u) / 8u;
        n.addr = utils::readBytes<uint32_t>(buf, bytes);
        return 1u + bytes;
    }

private:
    /**
     * @brief Allocates and populates a RibEntry for a single install descriptor.
     *
     * Resolves the primary next-hop against the RIB. If resolution fails, or if
     * the resolved entry is a /32 host route and recursiveHost is disabled, the
     * entry is deleted and nullptr is returned. Additional ECMP next-hops are
     * added without failing the overall install.
     *
     * @param install  Route descriptor from AddressFamilyInstance.
     * @return Heap-allocated RibEntry on success; nullptr if the next-hop is unresolvable.
     */
    core::RibEntry<uint32_t>* buildRoute(const NlriInstall& install)
    {
        core::RibEntry<uint32_t>* entry = new core::RibEntry<uint32_t>;
        entry->prefix        = install.route.route.nlri.addr;
        entry->length        = install.route.route.nlri.prefixLength;
        entry->source        = core::RouteSource::BGP;
        entry->processId     = bgp::ProcessAccessor::getAsNum(process);
        entry->adminDistance = install.distance;
        entry->metric        = install.metric;

        auto addHop = [&](const bgp::InboundRoute<types::IPv4Prefix>& r) -> bool
        {
            auto attrs = r.getPathAttributes();
            auto& nh = attrs.path.nextHop;
            if (!nh.isIPv4()) return false;
            uint32_t addr = nh.v4();
            {
                utils::RCU::Guard g;
                const core::RibEntry<uint32_t>* nhEntry = rib.lookup(nh.v4raw(), g);
                if (!nhEntry)
                    return false;
                if (!install.recursiveHost && nhEntry->length == 32)
                    return false;
                entry->addNextHop(addr, nhEntry->nextHops[0].iface);
            }
            return true;
        };

        if (!addHop(install.route.route))
        {
            delete entry;
            return nullptr;
        }
        for (const auto& hop : install.route.multipaths)
            if (hop) addHop(*hop);

        return entry;
    }

    core::RoutingTable& rib; ///< VRF routing table that receives installed routes.
};

namespace bgp
{

// NLRI POLICY VALIDATION

/// @brief Helper to extract the element types from a std::variant.
template <typename Variant>
struct VariantTypes;

template <typename... Ts>
struct VariantTypes<std::variant<Ts...>>
{
    using Types = std::tuple<Ts...>;
};

/**
 * @brief Compile-time verification that a type satisfies the NLRI policy contract.
 *
 * Checks that T defines `Nlri`, `afi`, and `LocRib` typedefs and that the
 * required wire-format methods (`nlriEncodedSize`, `encodeNlri`, `decodeNlri`)
 * are present with the correct signatures. Called once per type during static
 * assertion of the Nlri variant.
 *
 * @tparam T  Candidate NLRI policy type to validate.
 */
template <typename T>
consteval void validateNlriPolicy()
{
    static_assert(requires { typename T::Nlri; }, "NLRI policy must define 'using Nlri = ...'");
    static_assert(requires { T::afi; }, "NLRI policy must define static member 'afi'");
    static_assert(requires { typename T::LocRib; }, "NLRI policy must define 'using LocRib = ...'");

    using N = typename T::Nlri;

    static_assert(requires(const T& obj, const N& cn) { { obj.nlriEncodedSize(cn) } -> std::same_as<size_t>; },
                  "Missing method size_t nlriEncodeSize(const N&)");
    static_assert(requires(const T& obj, const N& cn, uint8_t* buf) { { obj.encodeNlri(buf, cn) } -> std::same_as<void>; },
                  "Missing method: void encodeNlri(uint8_t*, const N&)");
    static_assert(requires(const T& obj, const uint8_t* buf, N& n) { { obj.decodeNlri(buf, n) } -> std::same_as<size_t>; },
                  "Missing method: size_t decodeNlri(uint8_t*, N&)");
}

/**
 * @brief Validates every alternative in an NLRI variant at compile time.
 *
 * Called once via static_assert on the `Nlri` variant type definition below.
 * New NLRI policies added to the variant will automatically be checked.
 *
 * @tparam Ts  All alternative NLRI policy types in the variant.
 */
template <typename... Ts>
consteval void validateNlriVariant(std::variant<Ts...>*)
{
    (validateNlriPolicy<Ts>(), ...);
}

// ACTIVE NLRI VARIANT

/**
 * @brief Discriminated union of all active NLRI policy types in this build.
 *
 * AddressFamilyInstance is instantiated for each alternative. To add a new
 * address family, add its NLRI policy class as an alternative here and provide
 * a concrete NlriPolicy subclass (like ExampleNlri) that implements route
 * installation for that family.
 */
using Nlri = std::variant<
    ExampleNlri
>;

static_assert((validateNlriVariant((Nlri*)nullptr), true));

} // namespace bgp

} // namespace routing

#endif // BGP_NLRI_HPP

