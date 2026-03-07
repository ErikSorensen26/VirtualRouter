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
    ExampleNlri(VirtualRouter& vrf) : NlriPolicy<IPPrefix, BGP::AfiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST}>(vrf) {}

    void installRoute(BGP::RouteCanidate<IPPrefix>& nlri) override {}
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
template <typename T>
concept IsNlriPolicy =
requires { typename T::Nlri; { T::afi };} &&
std::derived_from<T, NlriPolicy<typename T::Nlri, T::afi>>;

template <typename Variant>
struct ValidateNlriVariant;

template <typename... Ts>
struct ValidateNlriVariant<std::variant<Ts...>>
{
    static constexpr bool value = (IsNlriPolicy<Ts> && ...);
};

using Nlri = std::variant<
    ExampleNlri
>;

static_assert(ValidateNlriVariant<Nlri>::value, "All types in Nlri must publicly inherit from NlriPolicy");
}

#endif // BGP_NLRI_HPP
