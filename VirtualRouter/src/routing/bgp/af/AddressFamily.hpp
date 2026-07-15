/**
 * @file AddressFamily.hpp
 * @brief Compile-time AFI/SAFI dispatch: maps an `AfiSafi` constant to the
 *        concrete `AddressFamilyInstance` specialisation that handles it.
 */

#ifndef BGP_ADDRESS_FAMILY__HPP
#define BGP_ADDRESS_FAMILY__HPP

#include "Nlri.hpp"

namespace routing::bgp
{
template <typename T>
class AddressFamilyInstance;

// DETAIL TRAITS

namespace detail
{
/**
 * @brief Transforms a `std::variant<T0, T1, ...>` of NLRI policy types into
 *        `std::variant<AddressFamilyInstance<T0>, AddressFamilyInstance<T1>, ...>`.
 *
 * @tparam Variant  A `std::variant` whose template arguments are NLRI policy types.
 */
template <typename Variant>
struct AddressFamilyVariant;

template <typename... Ts>
struct AddressFamilyVariant<std::variant<Ts...>>
{
    using type = std::variant<AddressFamilyInstance<Ts>...>; ///< Resulting variant of AF instances.
};

/**
 * @brief Walks a `std::variant` of NLRI policy types at compile time to find
 * @ingroup BGP_AF
 *        the one whose `afi` constant matches `AF`.
 *
 * A hard compile-time error is emitted when `AF` is not present in the variant
 * (i.e. the address family has not been registered in the `Nlri` variant).
 *
 * @tparam AF       The `AfiSafi` constant to search for.
 * @tparam Variant  The `std::variant` of NLRI policy candidates.
 */
template<AfiSafi AF, typename Variant>
struct AddressFamily;

/// @cond DETAIL
template <AfiSafi AF, typename T, typename... Rest>
struct AddressFamily<AF, std::variant<T, Rest...>>
    : std::conditional_t<(T::afi == AF),
          std::type_identity<T>,
          AddressFamily<AF, std::variant<Rest...>>>
{
    // inheritance keeps the non-matching branch uninstantiated; conditional_t on ::type would recurse eagerly and hit the terminator's static_assert
};

template <AfiSafi AF>
struct AddressFamily<AF, std::variant<>>
{
    static_assert(AF != AF, "Address family not found");
};
/// @endcond

/**
 * @brief Returns `true` when the compile-time constant `AF` is present in the
 * @ingroup BGP_AF
 *        `std::variant` pointed to by the (unused) pointer argument.
 *
 * @tparam AF   The `AfiSafi` constant to search for.
 * @tparam Ts   The NLRI policy types packed into the variant.
 */
template <AfiSafi AF, typename... Ts>
constexpr bool hasAddressFamily(std::variant<Ts...>*)
{
    return ((Ts::afi == AF) || ...);
}

/**
 * @brief Runtime overload: returns `true` when `af` matches any policy in the
 *        variant pointed to by the (unused) pointer argument.
 *
 * @tparam Ts   The NLRI policy types packed into the variant.
 * @param  af   The `AfiSafi` value to check at runtime.
 */
template <typename... Ts>
constexpr bool hasAddressFamily(AfiSafi& af, std::variant<Ts...>*)
{
    return ((Ts::afi == af) || ...);
}
} // namespace detail

// PUBLIC TYPE ALIASES

/// Variant that can hold any registered `AddressFamilyInstance` specialisation.
using AddressFamilyVariant = detail::AddressFamilyVariant<Nlri>::type;

/**
 * @brief Resolves the `AfiSafi` compile-time constant `AF` to the matching
 *        `AddressFamilyInstance` specialisation.
 *
 * Usage: `AddressFamily<BGP_AFI_IPV4_UNICAST>` yields
 * `AddressFamilyInstance<ExampleNlri>` (or whichever policy registered that
 * AFI/SAFI).  A hard static_assert fires when `AF` is unregistered.
 *
 * @tparam AF  An `AfiSafi` constant (e.g. `{BGP_AFI_IPV4, BGP_SAFI_UNICAST}`).
 */
template <AfiSafi AF>
using AddressFamily = AddressFamilyInstance<typename detail::AddressFamily<AF, Nlri>::type>;

/**
 * @brief Compile-time predicate: `true` when `AF` names a registered address family.
 *
 * @tparam AF  The `AfiSafi` constant to check.
 * @return `true` if the constant is present in the `Nlri` variant; `false` otherwise.
 */
template <AfiSafi AF>
inline constexpr bool hasAddressFamily()
{
    return detail::hasAddressFamily<AF>((AddressFamilyVariant*)nullptr);
}
} // namespace routing::bgp

#endif // BGP_ADDRESS_FAMILY_TYPES_HPP

