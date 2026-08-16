/**
 * @file OspfRegistry.h
 * @brief OSPF configuration registry: process, area, virtual-link, and OSPFv3 AF settings.
 * @ingroup OSPF
 *
 * Defines the configuration schema for OSPF including process parameters,
 * area configuration (types, authentication), router timers, default routes,
 * and virtual link settings.
 */

#ifndef OSPF_REGISTRY_H
#define OSPF_REGISTRY_H

#include <string>

#include "configs/EnumSchema.hpp"
#include "configs/RegistryBuilder.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/registry/policy/PolicyHelpers.hpp"

#include "IPAddress.h"
#include "OspfInterfaceRegistry.h"

namespace routing::ospf { class OspfProcess; class Area; }

namespace config
{
namespace ospf
{
#define OSPF_AREA_TYPE_MEMBERS(X) \
    X(NORMAL) \
    X(STUB) \
    X(NSSA) \

DEFINE_CONFIG_ENUM_NS(ospf, AreaType, OSPF_AREA_TYPE_MEMBERS);
}

#define OSPF_AREA_RANGE_FIELDS(X) \
    X(types::IPPrefix,          prefix) \
    X(bool,                     notAdvertise) \
    X(std::optional<uint32_t>,  cost)

DEFINE_TUPLE_SCHEMA(OspfAreaRange, OSPF_AREA_RANGE_FIELDS);

// DEFAULT_COST, FILTER_LIST, RANGE and VIRTUAL_LINKS carried no default row:
// absence is what those field kinds already store before configuration.
#define OSPF_AREA_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, IPSEC, OspfIPsecRegistry) \
    ATOMIC_FIELD(X, Y, AUTHENTICATION_TYPE, ospf::AuthType, ospf::AuthType::NULL_AUTH) \
    OPTIONAL_ATOMIC_FIELD(X, Y, DEFAULT_COST, uint32_t) \
    VALUE_FIELD(X, Y, FILTER_LIST_IN, std::string) TODO \
    VALUE_FIELD(X, Y, FILTER_LIST_OUT, std::string) TODO \
    ATOMIC_FIELD_CB(X, Y, AREA_TYPE, ospf::AreaType, ospf::AreaType::NORMAL) \
    ATOMIC_FIELD(X, Y, NSSA_DEFAULT_ORIGINATE, bool, false) \
    ATOMIC_FIELD(X, Y, NSSA_DEFAULT_METRIC, uint32_t, 1) \
    ATOMIC_FIELD(X, Y, NSSA_DEFAULT_METRIC_TYPE, bool, true) \
    ATOMIC_FIELD(X, Y, NSSA_DEFAULT_ONLY, bool, false) \
    ATOMIC_FIELD(X, Y, NSSA_NO_REDISTRIBUTION, bool, false) \
    ATOMIC_FIELD(X, Y, NSSA_ALWAYS_TRANSLATE, bool, false) \
    ATOMIC_FIELD(X, Y, NSSA_SUPPRESS_FA, bool, false) \
    ATOMIC_FIELD(X, Y, NO_EXT, bool, false) TODO \
    ATOMIC_FIELD(X, Y, NO_SUMMARY, bool, false) \
    ATOMIC_FIELD(X, Y, NO_TRANSIT, bool, false) TODO \
    LIST_FIELD_CB(X, Y, RANGE, OspfAreaRange) \
    OWNED_LIST_FIELD(X, Y, VIRTUAL_LINKS, OspfVirtualLinkRegistry, uint32_t) TODO

/**
 * @brief Per-area OSPF configuration fields (type, authentication, stub cost, NSSA).
 * @ingroup OSPF
 */
DEFINE_CONFIG_GROUP(OspfArea, OSPF_AREA_FIELD_LIST)

#define OSPF_TRAF_ENG_INTERFACE_FIELDS(X) \
    X(uint32_t, interfaceId) \
    X(uint32_t, area)

DEFINE_TUPLE_SCHEMA(OspfTrafEngInterface, OSPF_TRAF_ENG_INTERFACE_FIELDS);

#define OSPF_TRAF_ENG_MESH_GROUP_FIELDS(X) \
    X(uint32_t, groupId) \
    X(uint32_t, interfaceId) \
    X(uint32_t, area)

DEFINE_TUPLE_SCHEMA(OspfTrafEngMeshGroup, OSPF_TRAF_ENG_MESH_GROUP_FIELDS);

#define OSPF_NETWORK_FIELDS(X) \
    X(types::IPPrefix, prefix) \
    X(uint32_t,        area)

DEFINE_TUPLE_SCHEMA(OspfNetwork, OSPF_NETWORK_FIELDS);

#define OSPF_SUMMARY_ADDRESS_FIELDS(X) \
    X(types::IPPrefix,          prefix) \
    X(bool,                     notAdvertise) \
    X(bool,                     nssaOnly) \
    X(std::optional<uint32_t>,  tag)

DEFINE_TUPLE_SCHEMA(OspfSummaryAddress, OSPF_SUMMARY_ADDRESS_FIELDS);

#define OSPF_QUEUE_DEPTH_FIELDS(X) \
    X(bool, unlimited) \
    X(uint32_t, depth)

DEFINE_TUPLE_SCHEMA(OspfQueueDepth, OSPF_QUEUE_DEPTH_FIELDS);

#define OSPF_TABLE_MAP_FIELDS(X) \
    X(std::string, tableMap) \
    X(bool, filter)

struct OspfRegistry;

#define OSPF_FIELD_LIST(X, Y) \
    OPTIONAL_ATOMIC_FIELD(X, Y, PROCESS_ID, uint16_t) \
    OWNED_LIST_FIELD_CB_VA(X, Y, IPV4_INSTANCES, OspfRegistry, std::string) \
    OWNED_LIST_FIELD_CB_VA(X, Y, IPV6_INSTANCES, OspfRegistry, std::string) \
    OWNED_LIST_FIELD(X, Y, AREA_CONFIGS, OspfAreaRegistry, uint32_t) \
    ATOMIC_FIELD(X, Y, REFERENCE_BANDWIDTH, uint32_t, 100) \
    ATOMIC_FIELD(X, Y, BFD, bool, false) TODO \
    ATOMIC_FIELD(X, Y, LLS, bool, true) \
    ATOMIC_FIELD(X, Y, OPAQUE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, NORMAL, bool, true) TODO \
    ATOMIC_FIELD(X, Y, TRANSIT, bool, false) TODO /* virtual link */ \
    ATOMIC_FIELD(X, Y, NO_TRANSIT, bool, false) TODO /* virtual link */ \
    ATOMIC_FIELD(X, Y, DEFAULT_ORIGINATE_ALWAYS, bool, false) \
    ATOMIC_FIELD(X, Y, DEFAULT_ORIGINATE_METRIC, uint32_t, 1) \
    ATOMIC_FIELD(X, Y, DEFAULT_ORIGINATE_METRIC_TYPE, bool, true) \
    LIST_FIELD(X, Y, DEFAULT_ORIGINATE_ROUTE_MAP, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DEFAULT_METRIC, uint32_t) \
    ATOMIC_FIELD(X, Y, DISCARD_INTERNAL, bool, true) \
    ATOMIC_FIELD(X, Y, DISCARD_INTERNAL_DISTANCE, uint8_t, 110) \
    ATOMIC_FIELD(X, Y, DISCARD_EXTERNAL, bool, true) \
    ATOMIC_FIELD(X, Y, DISCARD_EXTERNAL_DISTANCE, uint8_t, 110) \
    ATOMIC_FIELD(X, Y, EXTERNAL_DISTANCE, uint8_t, 110) \
    ATOMIC_FIELD(X, Y, INTER_AREA_DISTANCE, uint8_t, 110) \
    ATOMIC_FIELD(X, Y, INTRA_AREA_DISTANCE, uint8_t, 110) \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_IN, policy::DistributeList) TODO \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_OUT, policy::DistributeList) TODO \
    ATOMIC_FIELD(X, Y, EVENT_LOG_ONE_SHOT, bool, false) TODO \
    ATOMIC_FIELD(X, Y, EVENT_LOG_PAUSE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, EVENT_LOG_SIZE, uint64_t, 0) TODO \
    ATOMIC_FIELD(X, Y, GRACEFUL_HELPER, bool, false) TODO \
    ATOMIC_FIELD(X, Y, GRACEFUL_STRICT_CHECKING, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IGNORE_MOSPF, bool, true) TODO \
    ATOMIC_FIELD(X, Y, SNMP_IFINDEX, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, RETRANSMISSION_DC_LIMIT, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, RETRANSMISSION_NON_DC_LIMIT, uint8_t) \
    ATOMIC_FIELD(X, Y, LOG_ADJACENCY_CHANGES, bool, false) \
    ATOMIC_FIELD(X, Y, LOG_ADJACENCY_DETAILS, bool, false) \
    ATOMIC_FIELD(X, Y, LRC_FORWARDING_ADDRESS, bool, true) \
    ATOMIC_FIELD(X, Y, LRC_INTER_AREA_SUMMARY, bool, true) \
    ATOMIC_FIELD(X, Y, LRC_NSSA_TRANSLATION, bool, false) \
    ATOMIC_FIELD(X, Y, MAX_METRIC_EXTERNAL, bool, false) \
    ATOMIC_FIELD(X, Y, MAX_METRIC_EXTERNAL_OVERRIDE, uint32_t, 16711680) \
    ATOMIC_FIELD(X, Y, MAX_METRIC_INCLUDE_STUB, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, MAX_METRIC_ON_STARTUP_TIME, uint16_t) \
    ATOMIC_FIELD(X, Y, MAX_METRIC_ON_STARTUP_WAIT_FOR_BGP, bool, false) \
    ATOMIC_FIELD(X, Y, MAX_METRIC_SUMMARY_LSA, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, MAX_LSA, uint32_t) \
    ATOMIC_FIELD(X, Y, MAX_LSA_THRESHOLD, uint8_t, 75) \
    OPTIONAL_ATOMIC_FIELD(X, Y, MAX_LSA_IGNORE_COUNT, uint16_t) \
    ATOMIC_FIELD(X, Y, MAX_LSA_IGNORE_TIME, uint16_t, 5) \
    OPTIONAL_ATOMIC_FIELD(X, Y, MAX_LSA_RESET_TIME, uint16_t) \
    ATOMIC_FIELD(X, Y, MAX_LSA_WARNING_ONLY, bool, false) \
    ATOMIC_FIELD(X, Y, MAXIMUM_PATHS, uint8_t, 4) \
    LIST_FIELD(X, Y, MPLS_LDP_AREAS, uint32_t) TODO \
    LIST_FIELD(X, Y, MPLS_TRAF_ENG_AREAS, uint32_t) TODO \
    LIST_FIELD(X, Y, MPLS_TRAF_ENG_INTERFACES, OspfTrafEngInterface) TODO \
    LIST_FIELD(X, Y, MPLS_TRAF_ENG_MESH_GROUP, OspfTrafEngMeshGroup) TODO \
    ATOMIC_FIELD(X, Y, MPLS_TRAF_ENG_MULTICAST_INACT, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, MPLS_TRAF_ENG_ROUTER_ID, uint32_t) TODO \
    LIST_FIELD_CB(X, Y, NETWORKS, OspfNetwork) \
    LIST_FIELD_CB(X, Y, NEIGHBORS, OspfNeighbor) \
    LIST_FIELD(X, Y, PASSIVE_INTERFACES, interface::InterfaceKey) TODO \
    LIST_FIELD(X, Y, PREFIX_PRIORITY_ROUTE_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, PREFIX_SUPPRESSION, bool, false) TODO \
    VALUE_FIELD(X, Y, HELLO_QUEUE_DEPTH, OspfQueueDepth) \
    VALUE_FIELD(X, Y, UPDATE_QUEUE_DEPTH, OspfQueueDepth) \
    OPTIONAL_ATOMIC_FIELD(X, Y, ROUTER_ID, uint32_t) \
    ATOMIC_FIELD(X, Y, SHUTDOWN, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, REDISTRIBUTE, std::nullptr_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, SNMP, std::nullptr_t) TODO \
    LIST_FIELD_CB(X, Y, SUMMARY_ADDRESS, OspfSummaryAddress) \
    ATOMIC_FIELD(X, Y, LSA_ARRIVAL, uint32_t, 1000) \
    ATOMIC_FIELD(X, Y, FLOOD_PACING, uint8_t, 33) \
    ATOMIC_FIELD(X, Y, LSA_GROUP_PACING, uint16_t, 240) \
    ATOMIC_FIELD(X, Y, RETRANSMISSION_PACING, uint8_t, 66) \
    ATOMIC_FIELD(X, Y, LSA_THROTTLE_DELAY, uint32_t, 0) \
    ATOMIC_FIELD(X, Y, LSA_THROTTLE_HOLD, uint32_t, 5000) \
    ATOMIC_FIELD(X, Y, LSA_THROTTLE_MAX, uint32_t, 5000) \
    ATOMIC_FIELD(X, Y, SPF_THROTTLE_DELAY, uint32_t, 5000) \
    ATOMIC_FIELD(X, Y, SPF_THROTTLE_HOLD, uint32_t, 10000) \
    ATOMIC_FIELD(X, Y, SPF_THROTTLE_MAX, uint32_t, 10000) \
    LIST_FIELD(X, Y, TABLE_MAP, policy::TableMap) TODO \
    ATOMIC_FIELD(X, Y, TRAFFIC_SHARE_MIN, bool, false) \
    ATOMIC_FIELD(X, Y, TTL_SEC, bool, false) \
    ATOMIC_FIELD(X, Y, TTL_SEC_HOPS, uint8_t, 1)

/**
 * @brief OSPF process-level configuration fields (areas, timers, redistribution, SPF tuning).
 * @ingroup OSPF
 */
DEFINE_CONFIG_GROUP(Ospf, OSPF_FIELD_LIST);
}

#endif // OSPF_REGISTRY_H
