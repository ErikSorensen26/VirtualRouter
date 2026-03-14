// Nlri.hpp

#ifndef BGP_NLRI_HPP
#define BGP_NLRI_HPP

#include <variant>
#include <IPAddress.hpp>

#include "NlriPolicy.hpp"
#include "bgp/BgpTypes.hpp"

class ExampleNlri : public BGP::NlriPolicy<IPPrefix, BGP::AfiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST}>
{
public:
    ExampleNlri(VirtualRouter& vrf) : BGP::NlriPolicy<IPPrefix, BGP::AfiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST}>(vrf) {}

    void installRoute(BGP::LocalRoute<IPPrefix>& nlri) override {}
    void withdrawRoute(const IPPrefix& nlri) override {}

    static size_t nlriEncodedSize(const IPPrefix& n)
    {
        return 1u + (static_cast<size_t>(n.prefixLength) + 7u) / 8u;
    }

    static void encodeNlri(uint8_t* buf, const IPPrefix& n)
    {
        buf[0] = n.prefixLength;
        size_t bytes = (static_cast<size_t>(n.prefixLength) + 7u) / 8u;
        std::memcpy(buf + 1, n.addr, bytes);
    }

    static size_t decodeNlri(const uint8_t* buf, IPPrefix& n)
    {
        n.prefixLength = buf[0];
        size_t bytes = (static_cast<size_t>(n.prefixLength) + 7u) / 8u;
        std::memset(n.addr, 0, sizeof(n.addr));
        std::memcpy(n.addr, buf + 1, bytes);
        n.af = AddressFamily::IPv4;
        return 1u + bytes;
    }
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
