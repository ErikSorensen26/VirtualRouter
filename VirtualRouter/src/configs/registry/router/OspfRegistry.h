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

#include "configs/RegistryBuilder.hpp"
#include "configs/TupleSchema.hpp"

#include "IPAddress.h"
#include "OspfInterfaceRegistry.h"
#include "configs/RegistryTypes.hpp"

namespace routing::ospf { class OspfProcess; class Area; }

namespace config
{
namespace ospf
{
/**
 * @brief OSPF area type controlling LSA flooding and default route origination.
 * @ingroup OSPF
 */
enum class AreaType
{
    NORMAL,
    STUB,
    TOTALLY_STUB, // stub no-summary
    NSSA, // NSSA
    TOTALLY_NSSA // NSSA_NO_SUMMARY
};
}

void OspfAreaTypeChange(void* area);
void OspfAreaSycnRanges(void* area);

#define OSPF_AREA_RANGE_FIELDS(X) \
    X(types::IPPrefix,          prefix) \
    X(bool,                     advertise) \
    X(std::optional<uint32_t>,  cost)

DEFINE_TUPLE_SCHEMA(OspfAreaRange, OSPF_AREA_RANGE_FIELDS);

// DEFAULT_COST, FILTER_LIST, RANGE and VIRTUAL_LINKS carried no default row:
// absence is what those field kinds already store before configuration.
#define OSPF_AREA_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, AUTHENTICATION_TYPE, ospf::AuthType, ospf::AuthType::NULL_AUTH) \
    OPTIONAL_ATOMIC_FIELD(X, Y, DEFAULT_COST, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, FILTER_LIST, std::nullptr_t) TODO \
    ATOMIC_FIELD_CB(X, Y, AREA_TYPE, ospf::AreaType, ospf::AreaType::NORMAL, OspfAreaTypeChange) \
    ATOMIC_FIELD(X, Y, NSSA_DEFAULT_ORIGINATE, bool, false) \
    ATOMIC_FIELD(X, Y, NSSA_DEFAULT_METRIC, uint32_t, 1) \
    ATOMIC_FIELD(X, Y, NSSA_DEFAULT_METRIC_TYPE, bool, true) \
    ATOMIC_FIELD(X, Y, NSSA_DEFAULT_ONLY, bool, false) \
    ATOMIC_FIELD(X, Y, NSSA_NO_EXT, bool, false) \
    ATOMIC_FIELD(X, Y, NSSA_NO_REDISTRIBUTION, bool, false) \
    ATOMIC_FIELD(X, Y, NSSA_ALWAYS_TRANSLATE, bool, false) \
    ATOMIC_FIELD(X, Y, NSSA_SUPPRESS_FA, bool, false) \
    LIST_FIELD_CB(X, Y, RANGE, OspfAreaRange, OspfAreaSycnRanges) \
    OWNED_LIST_FIELD(X, Y, VIRTUAL_LINKS, OspfVirtualLinkRegistry, uint32_t) TODO

/**
 * @brief Per-area OSPF configuration fields (type, authentication, stub cost, NSSA).
 * @ingroup OSPF
 */
DEFINE_CONFIG_GROUP(OspfArea, OSPF_AREA_FIELD_LIST)

void OspfSyncNeighbors(void* base);
void OspfSyncNetworks(void* base);
void OspfSyncSummaries(void* base);

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

#define OSPF_NEIGHBOR_FIELDS(X) \
    X(types::IPAddress,          address) \
    X(std::optional<uint16_t>,   cost) \
    X(std::optional<bool>,       databaseFilter) \
    X(std::optional<uint16_t>,   pollInterval) \
    X(std::optional<uint8_t>,    priority)

DEFINE_TUPLE_SCHEMA(OspfNeighbor, OSPF_NEIGHBOR_FIELDS);

#define OSPF_SUMMARY_ADDRESS_FIELDS(X) \
    X(types::IPPrefix,          prefix) \
    X(bool,                     advertise) \
    X(bool,                     nssaOnly) \
    X(std::optional<uint32_t>,  tag)

DEFINE_TUPLE_SCHEMA(OspfSummaryAddress, OSPF_SUMMARY_ADDRESS_FIELDS);

#define OSPF_FIELD_LIST(X, Y) \
    OWNED_LIST_FIELD(X, Y, AREA_CONFIGS, OspfAreaRegistry, uint32_t) \
    ATOMIC_FIELD(X, Y, REFERENCE_BANDWIDTH, uint32_t, 100) \
    ATOMIC_FIELD(X, Y, BFD, bool, false) TODO \
    ATOMIC_FIELD(X, Y, LLS, bool, true) \
    ATOMIC_FIELD(X, Y, OPAQUE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, TRANSIT, bool, false) TODO /* virtual link */ \
    OPTIONAL_ATOMIC_FIELD(X, Y, DOMAIN_ID, uint32_t) TODO \
    LIST_FIELD(X, Y, SECONDARY_DOMAIN_ID, uint32_t) TODO \
    ATOMIC_FIELD(X, Y, DEFAULT_ORIGINATE_ALWAYS, bool, false) \
    ATOMIC_FIELD(X, Y, DEFAULT_ORIGINATE_METRIC, uint32_t, 1) \
    ATOMIC_FIELD(X, Y, DEFAULT_ORIGINATE_METRIC_TYPE, bool, true) \
    LIST_FIELD(X, Y, DEFAULT_ORIGINATE_ROUTE_MAP, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DEFAULT_METRIC, uint32_t) \
    ATOMIC_FIELD(X, Y, DISCARD_INTERNAL, bool, true) \
    ATOMIC_FIELD(X, Y, DISCARD_INTERNAL_DISTANCE, uint8_t, 110) \
    ATOMIC_FIELD(X, Y, DISCARD_EXTERNAL, bool, true) \
    ATOMIC_FIELD(X, Y, DISCARD_EXTERNAL_DISTANCE, uint8_t, 110) \
    OPTIONAL_ATOMIC_FIELD(X, Y, DISTANCE, std::nullptr_t) \
    ATOMIC_FIELD(X, Y, EXTERNAL_DISTANCE, uint8_t, 110) \
    ATOMIC_FIELD(X, Y, INTER_AREA_DISTANCE, uint8_t, 110) \
    ATOMIC_FIELD(X, Y, INTRA_AREA_DISTANCE, uint8_t, 110) \
    OPTIONAL_ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST, std::nullptr_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DOMAIN_TAG, uint32_t) TODO \
    ATOMIC_FIELD(X, Y, EVENT_LOG_ONE_SHOT, bool, false) TODO \
    ATOMIC_FIELD(X, Y, EVENT_LOG_PAUSE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, EVENT_LOG_SIZE, uint64_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IGNORE_MOSPF, bool, true) TODO \
    ATOMIC_FIELD(X, Y, SNMP_IFINDEX, bool, false) TODO \
    ATOMIC_FIELD(X, Y, ISPF, bool, false) \
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
    LIST_FIELD_CB(X, Y, NETWORKS, OspfNetwork, OspfSyncNetworks) \
    LIST_FIELD_CB(X, Y, NEIGHBORS, OspfNeighbor, OspfSyncNeighbors) \
    ATOMIC_FIELD(X, Y, NSF_CISCO_HELPER, bool, false) TODO \
    ATOMIC_FIELD(X, Y, NSF_STRICT_CHECKING, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, HELLO_QUEUE_DEPTH, uint32_t) \
    LIST_FIELD(X, Y, PREFIX_PRIORITY_ROUTE_MAP, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, UPDATE_QUEUE_DEPTH, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, ROUTER_ID, uint32_t) \
    ATOMIC_FIELD(X, Y, SHUTDOWN, bool, false) \
    ATOMIC_FIELD(X, Y, LSA_ARRIVAL, uint32_t, 1000) \
    ATOMIC_FIELD(X, Y, FLOOD_PACING, uint8_t, 33) \
    ATOMIC_FIELD(X, Y, LSA_GROUP_PACING, uint16_t, 240) \
    ATOMIC_FIELD(X, Y, RETRANSMISSION_PACING, uint8_t, 66) \
    LIST_FIELD(X, Y, TABLE_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, TABLE_MAP_FILTER, bool, false) TODO \
    ATOMIC_FIELD(X, Y, PRIORITY, uint8_t, 1) \
    OPTIONAL_ATOMIC_FIELD(X, Y, REDISTRIBUTE, std::nullptr_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, SNMP, std::nullptr_t) TODO \
    LIST_FIELD_CB(X, Y, SUMMARY_ADDRESS, OspfSummaryAddress, OspfSyncSummaries) \
    ATOMIC_FIELD(X, Y, LSA_THROTTLE_DELAY, uint32_t, 0) \
    ATOMIC_FIELD(X, Y, LSA_THROTTLE_HOLD, uint32_t, 5000) \
    ATOMIC_FIELD(X, Y, LSA_THROTTLE_MAX, uint32_t, 5000) \
    ATOMIC_FIELD(X, Y, SPF_THROTTLE_DELAY, uint32_t, 5000) \
    ATOMIC_FIELD(X, Y, SPF_THROTTLE_HOLD, uint32_t, 10000) \
    ATOMIC_FIELD(X, Y, SPF_THROTTLE_MAX, uint32_t, 10000) \
    ATOMIC_FIELD(X, Y, TRAFFIC_SHARE_MIN, bool, false) \
    ATOMIC_FIELD(X, Y, TTL_SEC, bool, false) \
    ATOMIC_FIELD(X, Y, TTL_SEC_HOPS, uint8_t, 1)

/**
 * @brief OSPF process-level configuration fields (areas, timers, redistribution, SPF tuning).
 * @ingroup OSPF
 */
DEFINE_CONFIG_GROUP(Ospf, OSPF_FIELD_LIST)

#define OSPFV3_ADDRESS_FAMILY_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, IPV4, OspfRegistry) \
    REGISTRY_CONTAINER(X, Y, IPV6, OspfRegistry)

/**
 * @brief OSPFv3 address-family process container fields.
 * @ingroup OSPF
 */
DEFINE_CONFIG_GROUP(Ospfv3AddressFamily, OSPFV3_ADDRESS_FAMILY_FIELD_LIST)
}

#endif // OSPF_REGISTRY_H
