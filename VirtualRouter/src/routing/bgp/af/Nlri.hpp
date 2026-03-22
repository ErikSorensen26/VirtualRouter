// Nlri.hpp

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

using ExampleNlriType = BGP::NlriPolicy<IPv4Prefix, BGP::LocRibType::LPC_TRIE, BGP::AfiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST}>;

class ExampleNlri : public ExampleNlriType
{
public:
    ExampleNlri(VirtualRouter& vrf, BGP::BgpProcess& proc) : ExampleNlriType(vrf, proc),
        rib(vrf.getRib()) {}

    void installRoute(const NlriInstall& install) override
    {
        RibEntry<uint32_t>* route = buildRoute(install);
        if (!route) return;
        rib.addRoute(route);
    }

    void installRoutes(const std::vector<NlriInstall>& installs) override
    {
        std::vector<RibEntry<uint32_t>*> entries;
        entries.reserve(installs.size());
        
        for (const auto& install : installs)
        {
            RibEntry<uint32_t>* route = buildRoute(install);
            if (!route) continue;
            entries.push_back(route);
        }

        if (entries.empty()) return;

        rib.addRoutes(entries);
    }

    void withdrawRoute(const IPv4Prefix& nlri) override
    {
        rib.removeRoute(nlri.addr, nlri.prefixLength, RouteSource::BGP, BGP::ProcessAccessor::getAsNum(process));
    }

    void withdrawRoutes(const std::vector<Nlri>& nlri) override
    {
        std::vector<std::pair<uint32_t, uint8_t>> withdraws;
        withdraws.reserve(nlri.size());
        for (const auto& n : nlri)
            withdraws.push_back({n.addr, n.prefixLength});
        rib.removeRoutes(withdraws, RouteSource::BGP, BGP::ProcessAccessor::getAsNum(process));
    }

    static size_t nlriEncodedSize(const IPv4Prefix& n)
    {
        return 1u + (static_cast<size_t>(n.prefixLength) + 7u) / 8u;
    }

    static void encodeNlri(uint8_t* buf, const IPv4Prefix& n)
    {
        buf[0] = n.prefixLength;
        size_t bytes = (static_cast<size_t>(n.prefixLength) + 7u) / 8u;
        writeBytes(buf + 1, n.addr, bytes);
    }

    static size_t decodeNlri(const uint8_t* buf, IPv4Prefix& n)
    {
        n.prefixLength = buf[0];
        size_t bytes = (static_cast<size_t>(n.prefixLength) + 7u) / 8u;
        n.addr = readBytes<uint32_t>(buf, bytes);
        return 1u + bytes;
    }

private:
    RibEntry<uint32_t>* buildRoute(const NlriInstall& install)
    {
        RibEntry<uint32_t>* entry = new RibEntry<uint32_t>;
        entry->prefix        = install.route.route.nlri.addr;
        entry->length        = install.route.route.nlri.prefixLength;
        entry->source        = RouteSource::BGP;
        entry->processId     = BGP::ProcessAccessor::getAsNum(process);
        entry->adminDistance = install.distance;
        entry->metric        = install.metric;

        auto addHop = [&](const BGP::InboundRoute<IPv4Prefix>& r) -> bool
        {
            auto attrs = r.getPathAttributes();
            auto& nh = attrs.path.nextHop;
            if (!nh.isIPv4()) return false;
            uint32_t addr = nh.v4();
            const RibEntry<uint32_t>* nhEntry = rib.lookup(nh.v4raw());
            if (!nhEntry)
                return false;
            if (!install.recursiveHost && nhEntry->length == 32)
                return false;
            entry->addNextHop(addr, nhEntry->nextHops[0].iface);
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

    RoutingTable& rib;
};

namespace BGP
{
template <typename Variant>
struct VariantTypes;

template <typename... Ts>
struct VariantTypes<std::variant<Ts...>>
{
    using Types = std::tuple<Ts...>;
};

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

template <typename... Ts>
consteval void validateNlriVariant(std::variant<Ts...>*)
{
    (validateNlriPolicy<Ts>(), ...);
}

using Nlri = std::variant<
    ExampleNlri
>;

static_assert((validateNlriVariant((Nlri*)nullptr), true));
}

#endif // BGP_NLRI_HPP
