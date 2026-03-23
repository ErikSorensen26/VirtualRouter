// AddressFamily.hpp

#ifndef ADDRESS_FAMILY_HPP
#define ADDRESS_FAMILY_HPP

#include <cstdint>

namespace types
{

/**
 * @enum AddressFamily
 * @brief Enumerates the supported address families for routing.
 */
enum class AddressFamily : uint8_t
{
    NONE = 0,   ///< NONE, optional default value.
    IPv4 = 4,   ///< IPv4 address family.
    IPv6 = 16    ///< IPv6 address family.
};

} // namespace types

#endif // ADDRESS_FAMILY_HPP

