// AddressFamily.hpp

#ifndef ADDRESS_FAMILY_HPP
#define ADDRESS_FAMILY_HPP

/**
 * @enum AddressFamily
 * @brief Enumerates the supported address families for routing.
 */
enum class AddressFamily 
{
    NONE,   ///< NONE, optional default value.
    IPv4,   ///< IPv4 address family.
    IPv6    ///< IPv6 address family.
};

#endif // ADDRESS_FAMILY_HPP
