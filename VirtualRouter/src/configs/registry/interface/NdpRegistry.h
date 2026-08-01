/**
 * @file NdpRegistry.h
 * @brief Per-interface and global NDP configuration schema (RA, NUD, DAD, prefixes).
 * @ingroup CONFIG_INTERFACE
 */

#ifndef NDP_REGISTRY
#define NDP_REGISTRY

#include <IPAddress.h>
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryBuilder.hpp"
#include "configs/RegistryReference.hpp"

namespace config
{
namespace ndp
{
/**
 * @brief Router preference for default route selection (RFC 4191).
 * @ingroup CONFIG_INTERFACE
 */
enum class Preference { HIGH, MEDIUM, LOW };
}

// ROUTE_OWNER carries no default row in the old table; false is the value the
// zero-initialized field already reported, so stating it changes nothing.
#define NDP_BASE_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, CACHE_EXPIRE, uint16_t, 14400) \
    ATOMIC_FIELD(X, Y, CACHE_REFRESH, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, CACHE_INTERFACE_LIMIT, uint32_t) \
    ATOMIC_FIELD(X, Y, CACHE_INTERFACE_LIMIT_LOG_RATE, uint16_t, 1) \
    ATOMIC_FIELD(X, Y, DAD_TIME, uint16_t, 1000) \
    ATOMIC_FIELD(X, Y, HOST_MODE_STRICT, bool, false) \
    ATOMIC_FIELD(X, Y, NSF_CONVERGENCE_TIME, uint16_t, 30) \
    ATOMIC_FIELD(X, Y, NSF_DAD_SUPPRESS, uint16_t, 60) \
    ATOMIC_FIELD(X, Y, NSF_THROTTLE_RESOLUTIONS, uint16_t, 512) \
    ATOMIC_FIELD(X, Y, NUD_LIMIT, uint16_t, 400) \
    OPTIONAL_ATOMIC_FIELD(X, Y, NUD_REFRESH_PERIOD, uint16_t) \
    ATOMIC_FIELD(X, Y, REACHABLE_TIME, uint32_t, 300000) \
    ATOMIC_FIELD(X, Y, RESOLUTION_DATA_LIMIT, uint8_t, 16) \
    ATOMIC_FIELD(X, Y, ROUTE_OWNER, bool, false)

/**
 * @brief Global NDP cache and resolution tuning parameters.
 * @ingroup CONFIG_INTERFACE
 */
DEFINE_CONFIG_GROUP(NdpBase, NDP_BASE_FIELD_LIST)

// VALID_LIFETIME and PREFERRED_LIFETIME are optional fields: their old default
// rows named a value the field kind cannot hold, since absence is what the
// registry stores until a prefix is advertised. They stay defaultless here.
#define NDP_ENTRY_FIELD_LIST(X, Y) \
    OPTIONAL_ATOMIC_FIELD(X, Y, VALID_LIFETIME, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, PREFERRED_LIFETIME, uint32_t) \
    ATOMIC_FIELD(X, Y, NO_AUTOCONFIG, bool, true) \
    ATOMIC_FIELD(X, Y, NO_ONLINK, bool, true) \
    ATOMIC_FIELD(X, Y, NO_RTR_ADDRESS, bool, false) \
    ATOMIC_FIELD(X, Y, OFF_LINK, bool, false) \
    ATOMIC_FIELD(X, Y, NO_ADVERTISE, bool, false)

/**
 * @brief Per-prefix RA advertisement parameters (lifetime, autoconfig flags, on-link).
 * @ingroup CONFIG_INTERFACE
 */
DEFINE_CONFIG_GROUP(NdpEntry, NDP_ENTRY_FIELD_LIST)

#define NDP_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, BASE, NdpBaseRegistry) \
    ATOMIC_FIELD(X, Y, ADVERTISEMENT_INTERVAL, bool, false) \
    ATOMIC_FIELD(X, Y, AUTOCONFIG_DEFAULT_ROUTE, bool, false) \
    ATOMIC_FIELD(X, Y, AUTOCONFIG_PREFIX, bool, false) \
    ATOMIC_FIELD(X, Y, DAD_ATTEMPTS, uint16_t, 1) \
    ATOMIC_FIELD(X, Y, DESTINATION_GUARD, bool, false) \
    ATOMIC_FIELD(X, Y, MANAGED_CONFIG_FLAG, bool, false) \
    ATOMIC_FIELD(X, Y, NA_GLEAN, bool, false) \
    ATOMIC_FIELD(X, Y, NS_INTERVAL, uint32_t, 1000) \
    ATOMIC_FIELD(X, Y, NUD_IGP, bool, false) \
    ATOMIC_FIELD(X, Y, NUD_RETRY, uint8_t, 1) \
    ATOMIC_FIELD(X, Y, NUD_RETRY_INTERVAL, uint16_t, 1000) \
    ATOMIC_FIELD(X, Y, NUD_RETRY_ATTEMPTS, uint8_t, 3) \
    ATOMIC_FIELD(X, Y, NUD_FINAL_WAIT, uint16_t, 60000) \
    ATOMIC_FIELD(X, Y, OTHER_CONFIG_FLAG, bool, false) \
    OWNED_LIST_FIELD(X, Y, PREFIX_ENTRIES, NdpEntryRegistry, types::IPv6Prefix) \
    REGISTRY_CONTAINER(X, Y, PREFIX_DEFAULTS, NdpEntryRegistry) \
    ATOMIC_FIELD(X, Y, PREFIX_FRAMED_IPV6_PREFIX, bool, false) \
    ATOMIC_FIELD(X, Y, RA_HOP_LIMIT_UNSPECIFIED, bool, false) \
    ATOMIC_FIELD(X, Y, RA_INTERVAL, uint32_t, 200000) \
    ATOMIC_FIELD(X, Y, RA_MIN_INTERVAL, uint32_t, 150000) \
    ATOMIC_FIELD(X, Y, RA_LIFETIME, uint16_t, 1800) \
    ATOMIC_FIELD(X, Y, RA_MTU_SUPPRESS, bool, false) \
    ATOMIC_FIELD(X, Y, RA_SUPPRESS, bool, false) \
    ATOMIC_FIELD(X, Y, RA_SUPPRESS_ALL, bool, false) \
    ATOMIC_FIELD(X, Y, ROUTER_PREFERENCE, ndp::Preference, ndp::Preference::MEDIUM)

/**
 * @brief Per-interface NDP configuration fields (RA generation, DAD, NUD, prefix table).
 * @ingroup CONFIG_INTERFACE
 */
DEFINE_CONFIG_GROUP(Ndp, NDP_FIELD_LIST)
}

#endif // NDP_REGISTRY
