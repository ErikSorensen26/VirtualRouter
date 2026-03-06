// Nlri.hpp

#ifndef BGP_NLRI_HPP
#define BGP_NLRI_HPP

#include <variant>
#include <IPAddress.hpp>

#include "NlriPolicy.hpp"
#include "bgp/BgpTypes.hpp"

class ExampleNlri : public BGP::NlriPolicy<IPPrefix, BGP::AfiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST}>
{
    ExampleNlri(VirtualRouter& vrf) : NlriPolicy<IPPrefix, BGP::AfiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST}>(vrf) {}

    void installRoute(BGP::RouteCanidate<IPPrefix>& nlri) override {}
    void withdrawRoute(const IPPrefix& nlri) override {}
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
