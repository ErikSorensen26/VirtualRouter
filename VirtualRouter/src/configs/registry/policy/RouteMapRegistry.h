/**
 * @file RouteMapRegistry.h
 * @brief Route-map configuration schema: match conditions, set actions, and sequence ordering.
 * @ingroup CONFIG_POLICY
 */

#ifndef ROUTE_MAP_REGISTRY_H
#define ROUTE_MAP_REGISTRY_H

#include <routing/rib/RouteSource.hpp>
#include <IPAddress.h>

#include "routing/rib/RouteSource.hpp"
#include "configs/RegistryReference.hpp"
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryBuilder.hpp"
#include "configs/TupleSchema.hpp"
#include "interface/configs/InterfaceType.hpp"

#undef IPV6_NEXTHOP

namespace config
{
namespace policy
{
/**
 * @brief IS-IS / OSPF redistribution level for the `set level` route-map action.
 * @ingroup CONFIG_POLICY
 */
enum class Level
{
    LEVEL_1,   ///< Redistribute into IS-IS Level-1 or OSPF Level-1 only.
    LEVEL_2,   ///< Redistribute into IS-IS Level-2 or OSPF Level-2 only.
    LEVEL_1_2, ///< Redistribute into both levels.
    NSSA_ONLY  ///< Redistribute into OSPF NSSA areas only.
};

/**
 * @brief Route metric type for the `set metric-type` route-map action.
 * @ingroup CONFIG_POLICY
 */
enum class MetricType
{
    EXTERNAL, ///< EIGRP external metric.
    INTERNAL, ///< EIGRP internal metric.
    TYPE_1,   ///< OSPF external Type 1 (cost accumulates through AS).
    TYPE_2    ///< OSPF external Type 2 (cost does not accumulate).
};

/**
 * @brief BGP ORIGIN attribute value for the `set origin` route-map action.
 * @ingroup CONFIG_POLICY
 */
enum class Origin
{
    IGP,       ///< Route originated via IGP (well-known, mandatory).
    INCOMPLETE ///< Origin unknown — typically redistributed from another protocol.
};

/**
 * @brief Extended community cost attribute selector for `set extcommunity cost`.
 * @ingroup CONFIG_POLICY
 */
enum class ExtCommunityAttr
{
    IGP,           ///< Apply cost to the IGP metric field.
    PRE_BESTPATH   ///< Apply cost before the best-path selection process.
};

/**
 * @brief Arithmetic operation for the `set metric` route-map action.
 * @ingroup CONFIG_POLICY
 */
enum class MetricOperation
{
    NONE, ///< Set metric to an absolute value.
    ADD,  ///< Add to the existing metric.
    SUB   ///< Subtract from the existing metric.
};
}

/**
 * @brief Fields for the `set ip next-hop` / `set ipv6 next-hop` base block shared between
 *        `set base` and `set default` route-map actions.
 * @ingroup CONFIG_POLICY
 */
#define ROUTE_MAP_SEQUENCE_BASE_FIELD_LIST(X, Y) \
    LIST_FIELD(X, Y, INTERFACE, interface::InterfaceKey) \
    LIST_FIELD(X, Y, IP_GLOBAL_NEXTHOP, types::IPv4Address) \
    LIST_FIELD(X, Y, IP_NEXTHOP, types::IPv4Address) \
    VALUE_FIELD(X, Y, IP_VRF, std::string) \
    LIST_FIELD(X, Y, IPV6_GLOBAL_NEXTHOP, types::IPv6Address) \
    LIST_FIELD(X, Y, IPV6_NEXTHOP, types::IPv6Address) \
    VALUE_FIELD(X, Y, IPV6_VRF, std::string)

DEFINE_CONFIG_GROUP(RouteMapSequenceBase, ROUTE_MAP_SEQUENCE_BASE_FIELD_LIST)

#define ROUTE_MAP_APATHS_ADVERTISE_FIELDS(X) \
    X(bool, all) \
    X(bool, best) \
    X((std::optional<std::tuple<uint8_t, uint8_t>>), bestRange)

DEFINE_TUPLE_SCHEMA(RouteMapApathsAdvertise, ROUTE_MAP_APATHS_ADVERTISE_FIELDS);

#define ROUTE_MAP_METRIC_RANGE_FIELDS(X) \
    X(uint32_t, metric) \
    X(uint32_t, deviation)

DEFINE_TUPLE_SCHEMA(RouteMapMetricRange, ROUTE_MAP_METRIC_RANGE_FIELDS);

/**
 * @brief All configuration fields for a single route-map sequence entry.
 * @ingroup CONFIG_POLICY
 *
 * Fields are split into `MATCH_*` (conditions that must all be true for the sequence
 * to fire) and `SET_*` (actions applied when the sequence matches). The `PERMIT` field
 * controls whether a matching route is permitted or denied. `CONTINUE` redirects
 * evaluation to a different sequence number after this one fires.
 */
// Aliased because LIST_FIELD() is fixed arity and cannot absorb the type's comma.
using RouteMapSourceProtocol = std::pair<core::RouteSource, uint32_t>;

#define ROUTE_MAP_SEQUENCE_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, PERMIT, bool, true) \
    VALUE_FIELD(X, Y, DESCRIPTION, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, CONTINUE, uint16_t) \
    VALUE_FIELD(X, Y, MATCH_APATHS_ADVERTISE_SET, RouteMapApathsAdvertise) \
    LIST_FIELD(X, Y, MATCH_AS_PATH, uint16_t) \
    LIST_FIELD(X, Y, MATCH_COMMUNITY, std::string) \
    LIST_FIELD(X, Y, MATCH_EXTCOMMUNITY, std::string) \
    LIST_FIELD(X, Y, MATCH_INTERFACE, interface::InterfaceKey) \
    LIST_FIELD(X, Y, MATCH_IP_ADDRESS_ACL, std::string) \
    LIST_FIELD(X, Y, MATCH_IP_ADDRESS_PREFIX_LIST, std::string) \
    LIST_FIELD(X, Y, MATCH_IP_NEXT_HOP_ACL, std::string) \
    LIST_FIELD(X, Y, MATCH_IP_NEXT_HOP_PREFIX_LIST, std::string) \
    LIST_FIELD(X, Y, MATCH_IP_REDISTRIBUTION_SOURCE_ACL, std::string) \
    LIST_FIELD(X, Y, MATCH_IP_REDISTRIBUTION_SOURCE_PREFIX_LIST, std::string) \
    LIST_FIELD(X, Y, MATCH_IP_ROUTE_SOURCE_ACL, std::string) \
    LIST_FIELD(X, Y, MATCH_IP_ROUTE_SOURCE_PREFIX_LIST, std::string) \
    LIST_FIELD(X, Y, MATCH_IP_ROUTE_REDISTRIBUTION_SOURCE_ACL, std::string) \
    LIST_FIELD(X, Y, MATCH_IP_ROUTE_REDISTRIBUTION_SOURCE_PREFIX_LIST, std::string) \
    LIST_FIELD(X, Y, MATCH_IPV6_ADDRESS_ACL, std::string) \
    LIST_FIELD(X, Y, MATCH_IPV6_ADDRESS_PREFIX_LIST, std::string) \
    LIST_FIELD(X, Y, MATCH_IPV6_NEXT_HOP_ACL, std::string) \
    LIST_FIELD(X, Y, MATCH_IPV6_NEXT_HOP_PREFIX_LIST, std::string) \
    LIST_FIELD(X, Y, MATCH_IPV6_ROUTE_SOURCE_ACL, std::string) \
    LIST_FIELD(X, Y, MATCH_IPV6_ROUTE_SOURCE_PREFIX_LIST, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, MATCH_MIN_PACKET_LENGTH, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, MATCH_MAX_PACKET_LENGTH, uint32_t) \
    LIST_FIELD(X, Y, MATCH_LOCAL_PREFERENCE, uint32_t) \
    LIST_FIELD(X, Y, MATCH_MDT_GROUP, std::string) \
    LIST_FIELD(X, Y, MATCH_METRIC, RouteMapMetricRange) \
    LIST_FIELD(X, Y, MATCH_EXTERNAL_METRIC, RouteMapMetricRange) \
    ATOMIC_FIELD(X, Y, MATCH_MPLS_LABEL, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_ROUTE_TYPE_EXTERNAL_TYPE_1, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_ROUTE_TYPE_EXTERNAL_TYPE_2, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_ROUTE_TYPE_INTERNAL, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_ROUTE_TYPE_LEVEL_1, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_ROUTE_TYPE_LEVEL_2, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_ROUTE_TYPE_LOCAL, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_ROUTE_TYPE_NSSA_TYPE_1, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_ROUTE_TYPE_NSSA_TYPE_2, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_RKPI_INVALID, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_RKPI_NOT_FOUND, bool, false) \
    ATOMIC_FIELD(X, Y, MATCH_RKPI_VALID, bool, false) \
    LIST_FIELD(X, Y, MATCH_SOURCE_PROTOCOL, RouteMapSourceProtocol) \
    LIST_FIELD(X, Y, MATCH_TAG, uint32_t) \
    LIST_FIELD(X, Y, MATCH_TAG_LIST, std::string) \
    LIST_FIELD(X, Y, SET_AS_PATH_PREPEND, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_AS_PATH_PREPEND_LAST_AS, uint8_t) \
    ATOMIC_FIELD(X, Y, SET_AS_PATH_TAG, bool, false) \
    ATOMIC_FIELD(X, Y, SET_AUTOMATIC_TAG, bool, false) \
    VALUE_FIELD(X, Y, SET_COMM_LIST_DEL, std::string) \
    LIST_FIELD(X, Y, SET_COMMUNITY, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_EXTCOM_LIST_DEL, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_EXTCOMMUNITY_COST, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_EXTCOMMUNITY_COST_ID, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_EXTCOMMUNITY_COST_ATTR, policy::ExtCommunityAttr) \
    LIST_FIELD(X, Y, SET_EXTCOMMUNITY_RT, uint64_t) \
    ATOMIC_FIELD(X, Y, SET_EXTCOMMUNITY_RT_ADDITIVE, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_EXTCOMMUNITY_SOO, uint64_t) \
    ATOMIC_FIELD(X, Y, SET_GLOBAL, bool, false) \
    VALUE_FIELD(X, Y, SET_IP_ADDRESS_PREFIX_LIST, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_IP_DF, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_IP_PRECEDENCE, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_IP_QOS_GROUP, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_IP_TOS, uint8_t) \
    VALUE_FIELD(X, Y, SET_IPV6_ADDRESS_PREFIX_LIST, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_IPV6_PRECEDENCE, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_LEVEL, policy::Level) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_LOCAL_PREFERENCE, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_METRIC, uint32_t) \
    ATOMIC_FIELD(X, Y, SET_METRIC_OPERATION, policy::MetricOperation, policy::MetricOperation::NONE) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_METRIC_TYPE, policy::MetricType) \
    ATOMIC_FIELD(X, Y, SET_MPLS_LABEL, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_ORIGIN, policy::MetricOperation) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_TAG, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_TRAFFIC_INDEX, uint8_t) \
    VALUE_FIELD(X, Y, SET_VRF, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SET_WEIGHT, uint16_t) \
    REGISTRY_CONTAINER(X, Y, SET_BASE, RouteMapSequenceBaseRegistry) \
    REGISTRY_CONTAINER(X, Y, SET_DEFAULT, RouteMapSequenceBaseRegistry)

DEFINE_CONFIG_GROUP(RouteMapSequence, ROUTE_MAP_SEQUENCE_FIELD_LIST)

/**
 * @brief Top-level fields for a named route-map (ordered sequence of RouteMapSequenceRegistry entries).
 * @ingroup CONFIG_POLICY
 */
#define ROUTE_MAP_FIELD_LIST(X, Y) \
    OWNED_LIST_FIELD(X, Y, SEQUENCES, RouteMapSequenceRegistry, uint16_t)

DEFINE_CONFIG_GROUP(RouteMap, ROUTE_MAP_FIELD_LIST)

}

#endif // ROUTE_MAP_REGISTRY_H
