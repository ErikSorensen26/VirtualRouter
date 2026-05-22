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
#include "configs/RegistryDefaultTable.hpp"
#include "configs/SubRegistry.hpp"
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
enum class RouteMapSequenceBase
{
    INTERFACE,
    IP_GLOBAL_NEXTHOP,
    IP_NEXTHOP,
    IP_VRF,
    IPV6_GLOBAL_NEXTHOP,
    IPV6_NEXTHOP,
    IPV6_VRF,
    COUNT
};

/**
 * @brief Registry slot for the `set base` / `set default` next-hop block.
 * @ingroup CONFIG_POLICY
 */
struct RouteMapSequenceBaseRegistry
{
    SubRegistry<RouteMapSequenceBase, nullptr,
        ListField<interface::InterfaceKey CONFIG_INDEX_ARG(RouteMapSequenceBase::INTERFACE)>,
        ListField<types::IPv4Address CONFIG_INDEX_ARG(RouteMapSequenceBase::IP_GLOBAL_NEXTHOP)>,
        ListField<types::IPv4Address CONFIG_INDEX_ARG(RouteMapSequenceBase::IP_NEXTHOP)>,
        ValueField<std::string CONFIG_INDEX_ARG(RouteMapSequenceBase::IP_VRF)>,
        ListField<types::IPv6Address CONFIG_INDEX_ARG(RouteMapSequenceBase::IPV6_GLOBAL_NEXTHOP)>,
        ListField<types::IPv6Address CONFIG_INDEX_ARG(RouteMapSequenceBase::IPV6_NEXTHOP)>,
        ValueField<std::string CONFIG_INDEX_ARG(RouteMapSequenceBase::IPV6_VRF)>
    > reg;
};

/**
 * @brief All configuration fields for a single route-map sequence entry.
 * @ingroup CONFIG_POLICY
 *
 * Fields are split into `MATCH_*` (conditions that must all be true for the sequence
 * to fire) and `SET_*` (actions applied when the sequence matches). The `PERMIT` field
 * controls whether a matching route is permitted or denied. `CONTINUE` redirects
 * evaluation to a different sequence number after this one fires.
 */
enum class RouteMapSequence
{
    PERMIT,
    DESCRIPTION,
    CONTINUE,
    MATCH_APATHS_ADVERTISE_SET,
    MATCH_AS_PATH,
    MATCH_COMMUNITY,
    MATCH_EXTCOMMUNITY,
    MATCH_INTERFACE,
    MATCH_IP_ADDRESS_ACL,
    MATCH_IP_ADDRESS_PREFIX_LIST,
    MATCH_IP_NEXT_HOP_ACL,
    MATCH_IP_NEXT_HOP_PREFIX_LIST,
    MATCH_IP_REDISTRIBUTION_SOURCE_ACL,
    MATCH_IP_REDISTRIBUTION_SOURCE_PREFIX_LIST,
    MATCH_IP_ROUTE_SOURCE_ACL,
    MATCH_IP_ROUTE_SOURCE_PREFIX_LIST,
    MATCH_IP_ROUTE_REDISTRIBUTION_SOURCE_ACL,
    MATCH_IP_ROUTE_REDISTRIBUTION_SOURCE_PREFIX_LIST,
    MATCH_IPV6_ADDRESS_ACL,
    MATCH_IPV6_ADDRESS_PREFIX_LIST,
    MATCH_IPV6_NEXT_HOP_ACL,
    MATCH_IPV6_NEXT_HOP_PREFIX_LIST,
    MATCH_IPV6_ROUTE_SOURCE_ACL,
    MATCH_IPV6_ROUTE_SOURCE_PREFIX_LIST,
    MATCH_MIN_PACKET_LENGTH,
    MATCH_MAX_PACKET_LENGTH,
    MATCH_LOCAL_PREFERENCE,
    MATCH_MDT_GROUP,
    MATCH_METRIC,
    MATCH_EXTERNAL_METRIC,
    MATCH_MPLS_LABEL,
    MATCH_ROUTE_TYPE_EXTERNAL_TYPE_1,
    MATCH_ROUTE_TYPE_EXTERNAL_TYPE_2,
    MATCH_ROUTE_TYPE_INTERNAL,
    MATCH_ROUTE_TYPE_LEVEL_1,
    MATCH_ROUTE_TYPE_LEVEL_2,
    MATCH_ROUTE_TYPE_LOCAL,
    MATCH_ROUTE_TYPE_NSSA_TYPE_1,
    MATCH_ROUTE_TYPE_NSSA_TYPE_2,
    MATCH_RKPI_INVALID,
    MATCH_RKPI_NOT_FOUND,
    MATCH_RKPI_VALID,
    MATCH_SOURCE_PROTOCOL,
    MATCH_TAG,
    MATCH_TAG_LIST,
    SET_AS_PATH_PREPEND,
    SET_AS_PATH_PREPEND_LAST_AS,
    SET_AS_PATH_TAG,
    SET_AUTOMATIC_TAG,
    SET_COMM_LIST_DEL,
    SET_COMMUNITY,
    SET_DAMPENING,
    SET_EXTCOM_LIST_DEL,
    SET_EXTCOMMUNITY_COST,
    SET_EXTCOMMUNITY_COST_ID,
    SET_EXTCOMMUNITY_COST_ATTR,
    SET_EXTCOMMUNITY_RT,
    SET_EXTCOMMUNITY_RT_ADDITIVE,
    SET_EXTCOMMUNITY_SOO,
    SET_GLOBAL,
    SET_IP_ADDRESS_PREFIX_LIST,
    SET_IP_DF,
    SET_IP_PRECEDENCE,
    SET_IP_QOS_GROUP,
    SET_IP_TOS,
    SET_IPV6_ADDRESS_PREFIX_LIST,
    SET_IPV6_PRECEDENCE,
    SET_LEVEL,
    SET_LOCAL_PREFERENCE,
    SET_METRIC,
    SET_METRIC_OPERATION,
    SET_METRIC_TYPE,
    SET_MPLS_LABEL,
    SET_ORIGIN,
    SET_TAG,
    SET_TRAFFIC_INDEX,
    SET_VRF,
    SET_WEIGHT,
    SET_BASE,
    SET_DEFAULT,
    COUNT
};

#define ROUTE_MAP_SEQUENCE_DEFAULTS(X) \
    X(RouteMapSequence, PERMIT, true) \
    X(RouteMapSequence, MATCH_MPLS_LABEL, false) \
    X(RouteMapSequence, MATCH_ROUTE_TYPE_EXTERNAL_TYPE_1, false) \
    X(RouteMapSequence, MATCH_ROUTE_TYPE_EXTERNAL_TYPE_2, false) \
    X(RouteMapSequence, MATCH_ROUTE_TYPE_INTERNAL, false) \
    X(RouteMapSequence, MATCH_ROUTE_TYPE_LEVEL_1, false) \
    X(RouteMapSequence, MATCH_ROUTE_TYPE_LEVEL_2, false) \
    X(RouteMapSequence, MATCH_ROUTE_TYPE_LOCAL, false) \
    X(RouteMapSequence, MATCH_ROUTE_TYPE_NSSA_TYPE_1, false) \
    X(RouteMapSequence, MATCH_ROUTE_TYPE_NSSA_TYPE_2, false) \
    X(RouteMapSequence, MATCH_RKPI_INVALID, false) \
    X(RouteMapSequence, MATCH_RKPI_NOT_FOUND, false) \
    X(RouteMapSequence, MATCH_RKPI_VALID, false) \
    X(RouteMapSequence, SET_AS_PATH_TAG, false) \
    X(RouteMapSequence, SET_AUTOMATIC_TAG, false) \
    X(RouteMapSequence, SET_EXTCOMMUNITY_RT_ADDITIVE, false) \
    X(RouteMapSequence, SET_GLOBAL, false) \
    X(RouteMapSequence, SET_METRIC_OPERATION, policy::MetricOperation::NONE) \
    X(RouteMapSequence, SET_MPLS_LABEL, false)

CONFIG_DEFAULT_TABLE(ROUTE_MAP_SEQUENCE_DEFAULTS);

/**
 * @brief Registry slot for one route-map sequence entry.
 * @ingroup CONFIG_POLICY
 */
struct RouteMapSequenceRegistry
{
    SubRegistry<RouteMapSequence, nullptr,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::PERMIT)>,
        ValueField<std::string CONFIG_INDEX_ARG(RouteMapSequence::DESCRIPTION)>,
        OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(RouteMapSequence::CONTINUE)>,
        ValueField<std::tuple<bool, bool, std::optional<std::tuple<uint8_t, uint8_t>>> CONFIG_INDEX_ARG(RouteMapSequence::MATCH_APATHS_ADVERTISE_SET)>,
        ListField<uint16_t CONFIG_INDEX_ARG(RouteMapSequence::MATCH_AS_PATH)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_COMMUNITY)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_EXTCOMMUNITY)>,
        ListField<interface::InterfaceKey CONFIG_INDEX_ARG(RouteMapSequence::MATCH_INTERFACE)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IP_ADDRESS_ACL)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IP_ADDRESS_PREFIX_LIST)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IP_NEXT_HOP_ACL)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IP_NEXT_HOP_PREFIX_LIST)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IP_REDISTRIBUTION_SOURCE_ACL)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IP_REDISTRIBUTION_SOURCE_PREFIX_LIST)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IP_ROUTE_SOURCE_ACL)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IP_ROUTE_SOURCE_PREFIX_LIST)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IP_ROUTE_REDISTRIBUTION_SOURCE_ACL)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IP_ROUTE_REDISTRIBUTION_SOURCE_PREFIX_LIST)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IPV6_ADDRESS_ACL)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IPV6_ADDRESS_PREFIX_LIST)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IPV6_NEXT_HOP_ACL)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IPV6_NEXT_HOP_PREFIX_LIST)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IPV6_ROUTE_SOURCE_ACL)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_IPV6_ROUTE_SOURCE_PREFIX_LIST)>,
        OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::MATCH_MIN_PACKET_LENGTH)>,
        OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::MATCH_MAX_PACKET_LENGTH)>,
        ListField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::MATCH_LOCAL_PREFERENCE)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_MDT_GROUP)>,
        ListField<std::tuple<uint32_t, uint32_t> CONFIG_INDEX_ARG(RouteMapSequence::MATCH_METRIC)>,
        ListField<std::tuple<uint32_t, uint32_t> CONFIG_INDEX_ARG(RouteMapSequence::MATCH_EXTERNAL_METRIC)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_MPLS_LABEL)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_ROUTE_TYPE_EXTERNAL_TYPE_1)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_ROUTE_TYPE_EXTERNAL_TYPE_2)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_ROUTE_TYPE_INTERNAL)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_ROUTE_TYPE_LEVEL_1)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_ROUTE_TYPE_LEVEL_2)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_ROUTE_TYPE_LOCAL)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_ROUTE_TYPE_NSSA_TYPE_1)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_ROUTE_TYPE_NSSA_TYPE_2)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_RKPI_INVALID)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_RKPI_NOT_FOUND)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::MATCH_RKPI_VALID)>,
        ListField<std::pair<core::RouteSource, uint32_t> CONFIG_INDEX_ARG(RouteMapSequence::MATCH_SOURCE_PROTOCOL)>,
        ListField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::MATCH_TAG)>,
        ListField<std::string CONFIG_INDEX_ARG(RouteMapSequence::MATCH_TAG_LIST)>,
        ListField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::SET_AS_PATH_PREPEND)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(RouteMapSequence::SET_AS_PATH_PREPEND_LAST_AS)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::SET_AS_PATH_TAG)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::SET_AUTOMATIC_TAG)>,
        ValueField<std::string CONFIG_INDEX_ARG(RouteMapSequence::SET_COMM_LIST_DEL)>,
        ListField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::SET_COMMUNITY)>,
        ValueField<std::tuple<uint8_t, uint16_t, uint16_t, uint8_t> CONFIG_INDEX_ARG(RouteMapSequence::SET_DAMPENING)>,
        OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::SET_EXTCOM_LIST_DEL)>,
        OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::SET_EXTCOMMUNITY_COST)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(RouteMapSequence::SET_EXTCOMMUNITY_COST_ID)>,
        OptionalAtomicField<policy::ExtCommunityAttr CONFIG_INDEX_ARG(RouteMapSequence::SET_EXTCOMMUNITY_COST_ATTR)>,
        ListField<uint64_t CONFIG_INDEX_ARG(RouteMapSequence::SET_EXTCOMMUNITY_RT)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::SET_EXTCOMMUNITY_RT_ADDITIVE)>,
        OptionalAtomicField<uint64_t CONFIG_INDEX_ARG(RouteMapSequence::SET_EXTCOMMUNITY_SOO)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::SET_GLOBAL)>,
        ValueField<std::string CONFIG_INDEX_ARG(RouteMapSequence::SET_IP_ADDRESS_PREFIX_LIST)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(RouteMapSequence::SET_IP_DF)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(RouteMapSequence::SET_IP_PRECEDENCE)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(RouteMapSequence::SET_IP_QOS_GROUP)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(RouteMapSequence::SET_IP_TOS)>,
        ValueField<std::string CONFIG_INDEX_ARG(RouteMapSequence::SET_IPV6_ADDRESS_PREFIX_LIST)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(RouteMapSequence::SET_IPV6_PRECEDENCE)>,
        OptionalAtomicField<policy::Level CONFIG_INDEX_ARG(RouteMapSequence::SET_LEVEL)>,
        OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::SET_LOCAL_PREFERENCE)>,
        OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::SET_METRIC)>,
        AtomicField<policy::MetricOperation CONFIG_INDEX_ARG(RouteMapSequence::SET_METRIC_OPERATION)>,
        OptionalAtomicField<policy::MetricType CONFIG_INDEX_ARG(RouteMapSequence::SET_METRIC_TYPE)>,
        AtomicField<bool CONFIG_INDEX_ARG(RouteMapSequence::SET_MPLS_LABEL)>,
        OptionalAtomicField<policy::MetricOperation CONFIG_INDEX_ARG(RouteMapSequence::SET_ORIGIN)>,
        OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(RouteMapSequence::SET_TAG)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(RouteMapSequence::SET_TRAFFIC_INDEX)>,
        ValueField<std::string CONFIG_INDEX_ARG(RouteMapSequence::SET_VRF)>,
        OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(RouteMapSequence::SET_WEIGHT)>,
        RegistryContainer<RouteMapSequenceBaseRegistry CONFIG_INDEX_ARG(RouteMapSequence::SET_BASE)>,
        RegistryContainer<RouteMapSequenceBaseRegistry CONFIG_INDEX_ARG(RouteMapSequence::SET_DEFAULT)>
    > reg;
};

/**
 * @brief Top-level fields for a named route-map (ordered sequence of RouteMapSequenceRegistry entries).
 * @ingroup CONFIG_POLICY
 */
enum class RouteMap
{
    SEQUENCES, ///< Ordered list of route-map sequences keyed by sequence number.
    COUNT
};

/**
 * @brief Registry slot for one named route-map.
 * @ingroup CONFIG_POLICY
 */
struct RouteMapRegistry
{
    SubRegistry<RouteMap, nullptr,
        OwnedListField<RouteMapSequenceRegistry, uint16_t CONFIG_INDEX_ARG(RouteMap::SEQUENCES)>
    > reg;
};
}

#endif // ROUTE_MAP_REGISTRY_H
