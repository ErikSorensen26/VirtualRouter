// AddressFamilyTypes.hpp

#ifndef BGP_ADDRESS_FAMILY__HPP
#define BGP_ADDRESS_FAMILY__HPP

#include "AddressFamilyInstance.hpp"
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
bool hasAddressFamily(std::variant<Ts...>*)
{
    return ((Ts::afi == AF) || ...);
}
}

using AddressFamilyVariant = detail::AddressFamilyVariant<Nlri>;

template <AfiSafi AF>
using AddressFamily = typename detail::AddressFamily<AF, AddressFamily>::type;

template <AfiSafi AF>
inline constexpr bool hasAddressFamily = detail::hasAddressFamily<AF>((AddressFamilyVariant*)nullptr);
}

#endif // BGP_ADDRESS_FAMILY_TYPES_HPP
