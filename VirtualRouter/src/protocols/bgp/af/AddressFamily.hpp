// AddressFamilyTypes.hpp

#ifndef BGP_ADDRESS_FAMILY__HPP
#define BGP_ADDRESS_FAMILY__HPP

#include "AddressFamilyInstance.h"
#include "Nlri.hpp"

namespace BGP
{
namespace detail
{
template <typename Variant>
struct AddressFamilyVariant;

template <typename... Ts>
struct AddressFamilyVariant<std::variant<Ts...>>
{
    using type = std::variant<AddressFamilyInstance<Ts>...>;
};

template<AfiSafi AF, typename Variant>
struct AddressFamily;

template <AfiSafi AF, typename T, typename... Rest>
struct AddressFamily<AF, std::variant<T, Rest...>>
{
    using type = std::conditional_t<
        (T::afi == AF),
        T,
        typename AddressFamily<AF, std::variant<Rest...>>::type
    >;
};

template <AfiSafi AF>
struct AddressFamily<AF, std::variant<>>
{
    static_assert(AF != AF, "Address family not found");
};

template <AfiSafi AF, typename... Ts>
constexpr bool hasAddressFamily(std::variant<Ts...>*)
{
    return ((Ts::afi == AF) || ...);
}

template <typename... Ts>
constexpr bool hasAddressFamily(AfiSafi& af, std::variant<Ts...>*)
{
    return ((Ts::afi == af) || ...);
}
}

using AddressFamilyVariant = detail::AddressFamilyVariant<Nlri>::type;

template <AfiSafi AF>
using AddressFamily = AddressFamilyInstance<typename detail::AddressFamily<AF, Nlri>::type>;

template <AfiSafi AF>
inline constexpr bool hasAddressFamily()
{
    return detail::hasAddressFamily<AF>((AddressFamilyVariant*)nullptr);
}
}

#endif // BGP_ADDRESS_FAMILY_TYPES_HPP
