/**
 * @file NdpRegistry.h
 * @brief Ndp configuration registry
 *
 * Defines the configuration schema for NDP including...
 */

#ifndef NDP_REGISTRY
#define NDP_REGISTRY

#include <IPAddress.h>
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryDefaultTable.hpp"
#include "configs/RegistryReference.hpp"
#include "configs/SubRegistry.hpp"

namespace config
{
namespace ndp
{
/**
 * @enum Preference
 * @brief Router preference for default route selection (RFC 4191).
 */
enum class Preference { HIGH, MEDIUM, LOW };

/**
 * @enum RaGuardMode
 * @brief RA Guard protection level against unauthorized router announcements.
 */
enum class RaGuardMode : uint8_t { BLOCK_ALL, TRUSTED, MAC_WHITELIST};
}

enum class NdpBase
{
    CACHE_EXPIRE,
    CACHE_REFRESH,
    CACHE_INTERFACE_LIMIT,
    CACHE_INTERFACE_LIMIT_LOG_RATE,
    DAD_TIME,
    HOST_MODE_STRICT,
    NSF_CONVERGENCE_TIME,
    NSF_DAD_SUPPRESS,
    NSF_THROTTLE_RESOLUTIONS,
    NUD_LIMIT,
    NUD_REFRESH_PERIOD,
    REACHABLE_TIME,
    RESOLUTION_DATA_LIMIT,
    ROUTE_OWNER,
    COUNT
};

#define NDP_BASE_DEFAULTS(X) \
    X(NdpBase, CACHE_EXPIRE, 14400) \
    X(NdpBase, CACHE_REFRESH, false) \
    X(NdpBase, CACHE_INTERFACE_LIMIT_LOG_RATE, 1) \
    X(NdpBase, DAD_TIME, 1000) \
    X(NdpBase, HOST_MODE_STRICT, false) \
    X(NdpBase, NSF_CONVERGENCE_TIME, 30) \
    X(NdpBase, NSF_DAD_SUPPRESS, 60) \
    X(NdpBase, NSF_THROTTLE_RESOLUTIONS, 512) \
    X(NdpBase, NUD_LIMIT, 400) \
    X(NdpBase, NUD_REFRESH_PERIOD, 10) \
    X(NdpBase, REACHABLE_TIME, 300000) \
    X(NdpBase, RESOLUTION_DATA_LIMIT, 8) \
    X(NdpBase, ROUTE_OWNER, true)

CONFIG_DEFAULT_TABLE(NDP_BASE_DEFAULTS);

using NdpBaseRegistry = SubRegistry<NdpBase,
    AtomicField<uint16_t CONFIG_INDEX_ARG(NdpBase::CACHE_EXPIRE)>,
    AtomicField<bool CONFIG_INDEX_ARG(NdpBase::CACHE_REFRESH)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(NdpBase::CACHE_INTERFACE_LIMIT)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(NdpBase::CACHE_INTERFACE_LIMIT_LOG_RATE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(NdpBase::DAD_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(NdpBase::HOST_MODE_STRICT)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(NdpBase::NSF_CONVERGENCE_TIME)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(NdpBase::NSF_DAD_SUPPRESS)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(NdpBase::NSF_THROTTLE_RESOLUTIONS)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(NdpBase::NUD_LIMIT)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(NdpBase::NUD_REFRESH_PERIOD)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(NdpBase::REACHABLE_TIME)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(NdpBase::RESOLUTION_DATA_LIMIT)>,
    AtomicField<bool CONFIG_INDEX_ARG(NdpBase::ROUTE_OWNER)>
>;

enum class NdpEntry
{
    VALID_LIFETIME,
    PREFERRED_LIFETIME,
    NO_AUTOCONFIG,
    NO_ONLINK,
    NO_RTR_ADDRESS,
    OFF_LINK,
    NO_ADVERTISE,
    COUNT
};

#define NDP_ENTRY_DEFAULTS(X) \
    X(NdpEntry, VALID_LIFETIME, 2592000) \
    X(NdpEntry, PREFERRED_LIFETIME, 604800) \
    X(NdpEntry, NO_AUTOCONFIG, true) \
    X(NdpEntry, NO_ONLINK, true) \
    X(NdpEntry, NO_RTR_ADDRESS, false) \
    X(NdpEntry, OFF_LINK, false) \
    X(NdpEntry, NO_ADVERTISE, false)

using NdpEntryRegistry = SubRegistry<NdpEntry,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(NdpEntry::VALID_LIFETIME)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(NdpEntry::PREFERRED_LIFETIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(NdpEntry::NO_AUTOCONFIG)>,
    AtomicField<bool CONFIG_INDEX_ARG(NdpEntry::NO_ONLINK)>,
    AtomicField<bool CONFIG_INDEX_ARG(NdpEntry::NO_RTR_ADDRESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(NdpEntry::OFF_LINK)>,
    AtomicField<bool CONFIG_INDEX_ARG(NdpEntry::NO_ADVERTISE)>
>;

enum class Ndp
{
    BASE,
    ADVERTISEMENT_INTERVAL,
    AUTOCONFIG_DEFAULT_ROUTE,
    AUTOCONFIG_PREFIX,
    DAD_ATTEMPTS,
    DESTINATION_GUARD,
    MANAGED_CONFIG_FLAG,
    NA_GLEAN,
    NS_INTERVAL,
    NUD_IGP,
    NUD_RETRY,
    NUD_RETRY_INTERVAL,
    NUD_RETRY_ATTEMPTS,
    NUD_FINAL_WAIT,
    OTHER_CONFIG_FLAG,
    PREFIX_ENTRIES,
    PREFIX_DEFAULTS,
    PREFIX_FRAMED_IPV6_PREFIX,
    RA_HOP_LIMIT_UNSPECIFIED,
    RA_INTERVAL,
    RA_MIN_INTERVAL,
    RA_LIFETIME,
    RA_MTU_SUPPRESS,
    RA_SUPPRESS,
    RA_SUPPRESS_ALL,
    ROUTER_PREFERENCE,
    COUNT
};

#define NDP_DEFAULTS(X) \
    X(Ndp, ADVERTISEMENT_INTERVAL, false) \
    X(Ndp, AUTOCONFIG_DEFAULT_ROUTE, false) \
    X(Ndp, AUTOCONFIG_PREFIX, false) \
    X(Ndp, DAD_ATTEMPTS, 1) \
    X(Ndp, DESTINATION_GUARD, false) \
    X(Ndp, MANAGED_CONFIG_FLAG, false) \
    X(Ndp, NA_GLEAN, false) \
    X(Ndp, NS_INTERVAL, 1000) \
    X(Ndp, NUD_IGP, false) \
    X(Ndp, NUD_RETRY, 3) \
    X(Ndp, NUD_RETRY_INTERVAL, 1000) \
    X(Ndp, NUD_RETRY_ATTEMPTS, 3) \
    X(Ndp, NUD_FINAL_WAIT, 60000) \
    X(Ndp, OTHER_CONFIG_FLAG, false) \
    X(Ndp, PREFIX_FRAMED_IPV6_PREFIX, false) \
    X(Ndp, RA_HOP_LIMIT_UNSPECIFIED, false) \
    X(Ndp, RA_INTERVAL, 200000) \
    X(Ndp, RA_MIN_INTERVAL, 150000) \
    X(Ndp, RA_LIFETIME, 1800) \
    X(Ndp, RA_MTU_SUPPRESS, false) \
    X(Ndp, RA_SUPPRESS, false) \
    X(Ndp, RA_SUPPRESS_ALL, false) \
    X(Ndp, ROUTER_PREFERENCE, ndp::Preference::MEDIUM)

CONFIG_DEFAULT_TABLE(NDP_DEFAULTS);

using NdpRegistry = SubRegistry<Ndp,
    ReferenceContainer<NdpBaseRegistry CONFIG_INDEX_ARG(Ndp::BASE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::ADVERTISEMENT_INTERVAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::AUTOCONFIG_DEFAULT_ROUTE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::AUTOCONFIG_PREFIX)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Ndp::DAD_ATTEMPTS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::DESTINATION_GUARD)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::MANAGED_CONFIG_FLAG)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::NA_GLEAN)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ndp::NS_INTERVAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::NUD_IGP)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ndp::NUD_RETRY)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Ndp::NUD_RETRY_INTERVAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ndp::NUD_RETRY_ATTEMPTS)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Ndp::NUD_FINAL_WAIT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::OTHER_CONFIG_FLAG)>,
    OwnedListField<NdpEntryRegistry, types::IPv6Prefix CONFIG_INDEX_ARG(Ndp::PREFIX_ENTRIES)>,
    ReferenceContainer<NdpEntryRegistry CONFIG_INDEX_ARG(Ndp::PREFIX_DEFAULTS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::PREFIX_FRAMED_IPV6_PREFIX)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::RA_HOP_LIMIT_UNSPECIFIED)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ndp::RA_INTERVAL)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ndp::RA_MIN_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Ndp::RA_LIFETIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::RA_MTU_SUPPRESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::RA_SUPPRESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ndp::RA_SUPPRESS_ALL)>,
    AtomicField<ndp::Preference CONFIG_INDEX_ARG(Ndp::ROUTER_PREFERENCE)>
>;
}

#endif // NDP_REGISTRY
