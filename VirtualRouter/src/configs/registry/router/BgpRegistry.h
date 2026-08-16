/**
 * @file BgpRegistry.h
 * @brief BGP configuration registry: scope, neighbor, AF, and transport settings.
 * @ingroup BGP
 *
 * Defines the configuration schema for BGP including scope-level parameters,
 * address-family activation and policy, per-neighbor session tuning, peer-group
 * and peer-template inheritance, and transport (keepalive, hold-time) settings.
 */

#ifndef BGP_REGISTRY_H
#define BGP_REGISTRY_H

#include <string>
#include <IPAddress.h>

#include "interface/configs/InterfaceType.hpp"
#include "configs/registry/policy/PolicyHelpers.hpp"
#include "types/EnumBitMap.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/EnumSchema.hpp"
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryBuilder.hpp"

namespace config
{
namespace bgp
{
#define BGP_SLOW_PEER_MODE(X) \
    X(STATIC) \
    X(DYNAMIC) \
    X(DYNAMIC_PERMANENT)

DEFINE_CONFIG_ENUM_NS(bgp, SlowPeerMode, BGP_SLOW_PEER_MODE);

#define BGP_CONNECTION_MODE(X) \
    X(ACTIVE) \
    X(PASSIVE)

DEFINE_CONFIG_ENUM_NS(bgp, BgpConnectionMode, BGP_CONNECTION_MODE);

#define BGP_LOCAL_AS(X) \
    X(NO_PREPEND, 0b001) \
    X(REPLACE_AS, 0b010) \
    X(DUAL_AS, 0b100)

DEFINE_CONFIG_VALUE_ENUM_NS(bgp, BgpLocalAsProps, BGP_LOCAL_AS, uint8_t);
}

/**
 * @brief Shared transport parameters inherited by BGP scope and neighbor sessions.
 * @ingroup BGP
 */
#define BGP_TRANSPORT_BASE_FIELD_LIST(X, Y) \
    ATOMIC_FIELD_CB(X, Y, KEEPALIVE_INTERVAL, uint16_t, 60) \
    ATOMIC_FIELD_CB(X, Y, HOLDTIME, uint16_t, 180) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, MINIMUM_HOLDTIME, uint16_t) \
    ATOMIC_FIELD_CB(X, Y, TRANSPORT_PATH_MTU_DISCOVERY, bool, false)

DEFINE_CONFIG_GROUP(BgpTransportBase, BGP_TRANSPORT_BASE_FIELD_LIST)

/**
 * @brief Shared address-family parameters inherited by BGP scope and neighbor AF configs.
 * @ingroup BGP
 */
#define BGP_AF_BASE_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, ADDITIONAL_PATHS_INSTALL, bool, false) TODO \
    ATOMIC_FIELD_CB(X, Y, ADDITIONAL_PATHS_RECEIVE, bool, false) \
    ATOMIC_FIELD_CB(X, Y, ADDITIONAL_PATHS_SEND, bool, false) \
    ATOMIC_FIELD_CB(X, Y, ADDITIONAL_PATHS_SELECT_ALL, bool, false) \
    ATOMIC_FIELD_CB(X, Y, ADDITIONAL_PATHS_SELECT_BACKUP, bool, false) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, ADDITIONAL_PATHS_SELECT_BEST, uint8_t) \
    ATOMIC_FIELD_CB(X, Y, ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL, bool, false) \
    ATOMIC_FIELD_CB(X, Y, ADDITIONAL_PATHS_SELECT_GROUP_BEST, bool, false) \
    ATOMIC_FIELD_CB(X, Y, DEFAULT_ORIGINATE, bool, false) \
    VALUE_FIELD(X, Y, DEFAULT_ORIGINATE_ROUTE_MAP, std::string) TODO \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_IN, policy::DistributeList) TODO \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_OUT, policy::DistributeList) TODO \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, SLOW_PEER_MODE, config::bgp::SlowPeerMode) \
    ATOMIC_FIELD_CB(X, Y, SLOW_PEER_DETECTION, bool, false) \
    ATOMIC_FIELD_CB(X, Y, SLOW_PEER_DETECTION_THRESHOLD, uint16_t, 300)

DEFINE_CONFIG_GROUP(BgpAfBase, BGP_AF_BASE_FIELD_LIST)

#define BGP_REMOVE_PRIVATE_AS_FIELDS(X) \
    X(bool, all) \
    X(bool, replaceAs)

DEFINE_TUPLE_SCHEMA(BgpRemovePrivateAs, BGP_REMOVE_PRIVATE_AS_FIELDS);

/**
 * @brief Per-neighbor, per-address-family BGP configuration fields.
 * @ingroup BGP
 */
#define BGP_NEIGHBOR_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, AF_BASE, BgpAfBaseRegistry) \
    ATOMIC_FIELD_CB(X, Y, ACTIVATE, bool, false) \
    ATOMIC_FIELD_CB(X, Y, ADVERTISE_DIVERSE_PATH_BACKUP, bool, false) \
    ATOMIC_FIELD_CB(X, Y, ADVERTISE_DIVERSE_PATH_MPATH, bool, false) \
    VALUE_FIELD(X, Y, ADVERTISE_MAP, std::string) TODO \
    VALUE_FIELD(X, Y, ADVERTISE_MAP_EXIST_CONDITION, std::string) TODO \
    VALUE_FIELD(X, Y, ADVERTISE_MAP_NON_EXIST_CONDITION, std::string) TODO \
    ATOMIC_FIELD_CB(X, Y, ADVERTISE_INTERVAL, uint16_t, 30) \
    ATOMIC_FIELD_CB(X, Y, ALLOWAS_IN, bool, false) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, ALLOWAS_IN_OCCURANCES, uint8_t) \
    ATOMIC_FIELD(X, Y, ANNOUNCE_RPKI_STATE, bool, false) TODO \
    ATOMIC_FIELD_CB(X, Y, ORF_BOTH, bool, false) \
    ATOMIC_FIELD_CB(X, Y, ORF_RECEIVE, bool, false) \
    ATOMIC_FIELD_CB(X, Y, ORF_SEND, bool, false) \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_IN, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_IN_INTERFACE, interface::InterfaceKey) TODO \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_OUT, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_OUT_INTERFACE, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, DMZLINK_BW, bool, false) TODO \
    VALUE_FIELD(X, Y, FILTER_LIST_IN, std::string) TODO \
    VALUE_FIELD(X, Y, FILTER_LIST_OUT, std::string) TODO \
    VALUE_FIELD_CB(X, Y, INHERIT_PEER_POLICY, std::string) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, MAXIMUM_PREFIX, uint32_t) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, MAXIMUM_PREFIX_THRESHOLD, uint8_t) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, MAXIMUM_PREFIX_RESTART, uint16_t) \
    ATOMIC_FIELD_CB(X, Y, MAXIMUM_PREFIX_WARNING_ONLY, bool, false) \
    ATOMIC_FIELD_CB(X, Y, NEXT_HOP_SELF, bool, false) \
    ATOMIC_FIELD_CB(X, Y, NEXT_HOP_SELF_ALL, bool, false) \
    ATOMIC_FIELD_CB(X, Y, NEXT_HOP_UNCHANGED, bool, false) \
    VALUE_FIELD(X, Y, PREFIX_LIST_IN, std::string) TODO \
    VALUE_FIELD(X, Y, PREFIX_LIST_OUT, std::string) TODO \
    VALUE_FIELD_CB(X, Y, REMOVE_PRIVATE_AS, BgpRemovePrivateAs) \
    VALUE_FIELD(X, Y, ROUTE_MAP_IN, std::string) TODO \
    VALUE_FIELD(X, Y, ROUTE_MAP_OUT, std::string) TODO \
    ATOMIC_FIELD_CB(X, Y, ROUTE_REFLECTOR_CLIENT, bool, false) \
    ATOMIC_FIELD(X, Y, ROUTE_SERVER_CLIENT, bool, false) TODO \
    VALUE_FIELD(X, Y, ROUTE_SERVER_CLIENT_CONTEXT, std::string) TODO \
    ATOMIC_FIELD_CB(X, Y, SEND_COMMUNITY, bool, false) \
    ATOMIC_FIELD_CB(X, Y, SEND_COMMUNITY_BOTH, bool, false) \
    ATOMIC_FIELD_CB(X, Y, SEND_COMMUNITY_EXTENDED, bool, false) \
    ATOMIC_FIELD_CB(X, Y, SEND_COMMUNITY_STANDARD, bool, false) \
    ATOMIC_FIELD_CB(X, Y, SEND_LABEL, bool, false) \
    ATOMIC_FIELD_CB(X, Y, SEND_LABEL_EXPLICIT_NULL, bool, false) \
    ATOMIC_FIELD_CB(X, Y, SOFT_RECONFIGURATION, bool, false) \
    ATOMIC_FIELD(X, Y, TRANSLATE_UPDATE_MULTICAST, bool, false) TODO \
    ATOMIC_FIELD(X, Y, TRANSLATE_UPDATE_UNICAST, bool, false) TODO \
    VALUE_FIELD(X, Y, UNSUPPRESS_MAP, std::string) TODO \
    VALUE_FIELD_CB(X, Y, WEIGHT, uint16_t)

DEFINE_CONFIG_GROUP(BgpNeighbor, BGP_NEIGHBOR_FIELD_LIST)

#define BGP_PATH_ATTRIBUTE_FIELDS(X) \
    X(uint8_t, start) \
    X(uint8_t, end)

DEFINE_TUPLE_SCHEMA(BgpPathAttribute, BGP_PATH_ATTRIBUTE_FIELDS);

#define BGP_LOCAL_AS_FIELDS(X) \
    X(uint32_t, as) \
    X(types::EnumBitMap<bgp::BgpLocalAsProps>, props)

DEFINE_TUPLE_SCHEMA(BgpLocalAs, BGP_LOCAL_AS_FIELDS);

/**
 * @brief Session-level BGP neighbor configuration fields (transport, timers, auth, path attributes).
 * @ingroup BGP
 */
#define BGP_NEIGHBOR_SESSION_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, BGP_BASE, BgpTransportBaseRegistry) \
    VALUE_FIELD(X, Y, DESCRIPTION, std::string) TODO \
    ATOMIC_FIELD_CB(X, Y, DISABLE_CONNECTION_CHECK, bool, false) \
    ATOMIC_FIELD(X, Y, EBGP_MULTIHOP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, EBGP_MAX_HOP_COUNT, uint8_t, 1) TODO \
    ATOMIC_FIELD(X, Y, FALL_OVER, bool, false) TODO \
    ATOMIC_FIELD(X, Y, FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, FALL_OVER_BFD_MULTI_HOP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, FALL_OVER_BFD_SINGLE_HOP, bool, false) TODO \
    VALUE_FIELD(X, Y, FALL_OVER_ROUTE_MAP, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, HAMODE_GRACEFUL_RESTART, bool) TODO \
    VALUE_FIELD_CB(X, Y, INHERIT_PEER_SESSION, std::string) \
    VALUE_FIELD_CB(X, Y, LOCAL_AS, BgpLocalAs) \
    VALUE_FIELD(X, Y, PASSWORD, std::string) TODO \
    LIST_FIELD_CB(X, Y, PATH_ATTRIBUTE_DISCARD, BgpPathAttribute) \
    LIST_FIELD_CB(X, Y, PATH_ATTRIBUTE_TREAT_AS_WITHDRAW, BgpPathAttribute) TODO \
    VALUE_FIELD_CB(X, Y, PEER_GROUP, std::string) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, REMOTE_AS, uint32_t) \
    ATOMIC_FIELD_CB(X, Y, SHUTDOWN, bool, false) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, TRANSPORT_CONNECTION_MODE, bgp::BgpConnectionMode) \
    ATOMIC_FIELD_CB(X, Y, TRANSPORT_MULTI_SESSION, bool, false) \
    ATOMIC_FIELD(X, Y, TTL_SEC, bool, false) TODO \
    ATOMIC_FIELD(X, Y, TTL_SEC_HOP, uint8_t, 1) TODO \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, UPDATE_SOURCE, interface::InterfaceKey)

DEFINE_CONFIG_GROUP(BgpNeighborSession, BGP_NEIGHBOR_SESSION_FIELD_LIST)

#define BGP_AGGREGATE_ADDRESS_FIELDS(X) \
    X(types::IPPrefix,    prefix) \
    X(IGNOR(std::string), advertiseMap) \
    X(IGNOR(bool),        asConfedSet) \
    X(IGNOR(bool),        asSet) \
    X(IGNOR(std::string), attributeMap) \
    X(IGNOR(std::string), routeMap) \
    X(IGNOR(bool),        summaryOnly) \
    X(IGNOR(std::string), suppressMap)

DEFINE_TUPLE_SCHEMA(BgpAggregateAddress, BGP_AGGREGATE_ADDRESS_FIELDS);

#define BGP_DISTANCE_RANGE_FIELDS(X) \
    X(uint8_t,         distance) \
    X(types::IPPrefix, prefix) \
    X(IGNOR(std::string),     list)

DEFINE_TUPLE_SCHEMA(BgpDistanceRange, BGP_DISTANCE_RANGE_FIELDS);

#define BGP_NETWORK_FIELDS(X) \
    X(types::IPPrefix, prefix) \
    X(bool,            backdoor) \
    X(std::string,     routeMap)

DEFINE_TUPLE_SCHEMA(BgpNetwork, BGP_NETWORK_FIELDS);

/**
 * @brief Process-level BGP address-family configuration fields (network statements, redistribution, best-path).
 * @ingroup BGP
 */
#define BGP_ADDRESS_FAMILY_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, AF_BASE, BgpAfBaseRegistry) \
    LIST_FIELD_CB(X, Y, AGGREGATE_ADDRESS, BgpAggregateAddress) \
    ATOMIC_FIELD_CB(X, Y, BGP_AGGREGATE_TIMER, uint16_t, 30) \
    ATOMIC_FIELD_CB(X, Y, BGP_BEST_PATH_IGP_METRIC_IGNORE, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_DMZLINK_BW, bool, false) TODO \
    VALUE_FIELD(X, Y, BGP_INJECT_MAP, std::string) TODO \
    VALUE_FIELD(X, Y, BGP_INJECT_MAP_EXIST_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, BGP_INJECT_MAP_COPY_ATTRIBUTES, bool, false) TODO \
    VALUE_FIELD(X, Y, BGP_NEXT_HOP_ROUTE_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, BGP_NEXT_HOP_TRIGGER, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_NEXT_HOP_TRIGGER_DELAY, uint16_t, 5) \
    ATOMIC_FIELD(X, Y, BGP_NEXT_HOP_TRACKING, bool, true) \
    ATOMIC_FIELD(X, Y, BGP_RECURSIVE_HOST, bool, true) \
    ATOMIC_FIELD(X, Y, BGP_REDISTRIBUTE_INTERNAL, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_ROUTE_MAP_PRIORITY, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_SOFT_RECONFIG_BACKUP, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DEFAULT_METRIC, uint32_t) \
    LIST_FIELD_CB(X, Y, DISTANCE_RANGE, BgpDistanceRange) \
    ATOMIC_FIELD_CB(X, Y, DISTANCE_BGP_EXTERNAL, uint8_t, 20) \
    ATOMIC_FIELD_CB(X, Y, DISTANCE_BGP_INTERNAL, uint8_t, 200) \
    ATOMIC_FIELD_CB(X, Y, DISTANCE_BGP_LOCAL, uint8_t, 200) \
    ATOMIC_FIELD_CB(X, Y, DISTANCE_MBGP_EXTERNAL, uint8_t, 20) \
    ATOMIC_FIELD_CB(X, Y, DISTANCE_MBGP_INTERNAL, uint8_t, 200) \
    ATOMIC_FIELD_CB(X, Y, DISTANCE_MBGP_LOCAL, uint8_t, 200) \
    ATOMIC_FIELD_CB(X, Y, MAXIMUM_PATHS_EBGP, uint8_t, 1) \
    ATOMIC_FIELD_CB(X, Y, MAXIMUM_PATHS_IBGP, uint8_t, 1) \
    OWNED_LIST_FIELD(X, Y, NEIGHBOR, BgpNeighborRegistry, types::IPAddress) \
    LIST_FIELD_CB(X, Y, NETWORK, BgpNetwork) \
    OWNED_LIST_FIELD(X, Y, PEER_GROUP, BgpNeighborRegistry, std::string) \
    VALUE_FIELD(X, Y, TABLE_MAP, policy::TableMap) TODO \

DEFINE_CONFIG_GROUP(BgpAddressFamily, BGP_ADDRESS_FAMILY_FIELD_LIST)

#define BGP_AF_VRF_LIST(X, Y) \
    VALUE_FIELD(X, Y, VRF, std::string) \
    OPTIONAL_REGISTRY_CONTAINER_CB(X, Y, IPV4_UNICAST, BgpAddressFamilyRegistry) \
    OPTIONAL_REGISTRY_CONTAINER_CB(X, Y, IPV6_UNICAST, BgpAddressFamilyRegistry)

DEFINE_CONFIG_GROUP(BgpAfVrf, BGP_AF_VRF_LIST);

#define BGP_LISTEN_RANGE_FIELDS(X) \
    X(types::IPPrefix,    prefix) \
    X(IGNOR(std::string), peerGroup)

DEFINE_TUPLE_SCHEMA(BgpListenRange, BGP_LISTEN_RANGE_FIELDS);

#define BGP_RPKI_SERVER_FIELDS(X) \
    X(types::IPAddress, address) \
    X(uint16_t,         port) \
    X(uint16_t,         refreshTime) \
    X(std::string,      sshUsername) \
    X(std::string,      sshPassword) \
    X(bool,             localPort)

DEFINE_TUPLE_SCHEMA(BgpRpkiServer, BGP_RPKI_SERVER_FIELDS);

/**
 * @brief BGP scope configuration fields.
 * @ingroup BGP
 */
#define BGP_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, AUTONOMOUS_SYSTEM, uint32_t, 0) \
    REGISTRY_CONTAINER(X, Y, BGP_BASE, BgpTransportBaseRegistry) \
    OWNED_LIST_FIELD_CB(X, Y, AF_VRF, BgpAfVrfRegistry, std::string) \
    ATOMIC_FIELD_CB(X, Y, BGP_ALWAYS_COMPARE_MED, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_AS_DOT_NOTATION, bool, false) TODO \
    ATOMIC_FIELD_CB(X, Y, BGP_BEST_PATH_COMPARE_ROUTER_ID, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_BEST_PATH_COST_COMMUNITY_IGNORE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_BEST_PATH_MED_CONFED, bool, false) TODO \
    ATOMIC_FIELD_CB(X, Y, BGP_BEST_PATH_MED_MISSING_AS_WORST, bool, false) \
    ATOMIC_FIELD_CB(X, Y, BGP_CLIENT_TO_CLIENT_REFLECTION, bool, false) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, BGP_CLUSTER_ID, uint32_t) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, BGP_CONFEDERATION_IDENTIFIER, uint32_t) \
    LIST_FIELD_CB(X, Y, BGP_CONFEDERATION_PEERS, uint32_t) \
    ATOMIC_FIELD(X, Y, BGP_CONSISTENCY_CHECKER_AUTO_REPAIR, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_CONSISTENCY_CHECKER_AUTO_REPAIR_INTERVAL, uint32_t, 1440) TODO \
    ATOMIC_FIELD(X, Y, BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL, uint32_t, 1440) TODO \
    ATOMIC_FIELD(X, Y, BGP_DEFAULT_IPV4_UNICAST, bool, true) TODO \
    ATOMIC_FIELD(X, Y, BGP_DEFAULT_IPV6_NEXTHOP, bool, true) TODO \
    ATOMIC_FIELD(X, Y, BGP_DEFAULT_LOCAL_PREFERENCE, uint32_t, 100) TODO \
    ATOMIC_FIELD(X, Y, BGP_DEFAULT_ROUTE_TARGET_FILTER, bool, false) TODO \
    ATOMIC_FIELD_CB(X, Y, BGP_DETERMINISTIC_MED, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_DMZLINK_BW, bool, false) TODO \
    ATOMIC_FIELD_CB(X, Y, BGP_ENFORCE_FIRST_AS, bool, true) \
    ATOMIC_FIELD(X, Y, BGP_ENHANCED_ERROR, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_FAST_EXTERNAL_FAILOVER, bool, true) TODO \
    ATOMIC_FIELD(X, Y, BGP_GRACEFUL_RESTART, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_GRACEFUL_RESTART_EXTENDED, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_GRACEFUL_RESTART_TIME, uint16_t, 120) TODO \
    ATOMIC_FIELD(X, Y, BGP_GRACEFUL_RESTART_STALEPATH_TIME, uint16_t, 360) TODO \
    ATOMIC_FIELD(X, Y, BGP_LISTEN, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_LISTEN_LIMIT, uint16_t) \
    LIST_FIELD(X, Y, BGP_LISTEN_RANGE, BgpListenRange) \
    ATOMIC_FIELD(X, Y, BGP_LOG_NEIGHBOR_CHANGES, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_MAX_AS_LIMIT, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_MAX_COMMUNITY_LIMIT, uint16_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_MAX_EXT_COMMUNITY_LIMIT, uint16_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_NOPEERUP_DELAY_COLD_BOOT, uint16_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_NOPEERUP_DELAY_NSF_SWITCHOVER, uint16_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_NOPEERUP_DELAY_POST_BOOT, uint16_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_NOPEERUP_DELAY_USER_INITIATED, uint16_t) TODO \
    ATOMIC_FIELD(X, Y, BGP_REFRESH_MAX_EOR_TIME, uint16_t, 60) \
    ATOMIC_FIELD(X, Y, BGP_REFRESH_STALEPATH_TIME, uint16_t, 120) \
    ATOMIC_FIELD(X, Y, BGP_REGEX_DETERMINISTIC, bool, true) TODO \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, BGP_ROUTER_ID, uint32_t) \
    LIST_FIELD(X, Y, BGP_RPKI_SERVER, BgpRpkiServer) TODO \
    ATOMIC_FIELD(X, Y, BGP_SCAN_TIME, uint8_t, 60) \
    ATOMIC_FIELD_CB(X, Y, BGP_SUPPRESS_INACTIVE, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_UPDATE_DELAY, uint16_t) \
    OWNED_LIST_FIELD_CB(X, Y, NEIGHBOR, BgpNeighborSessionRegistry, types::IPAddress) \
    OWNED_LIST_FIELD_CB(X, Y, PEER_GROUP, BgpNeighborSessionRegistry, std::string) \
    VALUE_FIELD(X, Y, ROUTE_SERVER_CONTEXT, std::string) TODO \
    OWNED_LIST_FIELD(X, Y, TEMPLATE_PEER_POLICY, BgpNeighborRegistry, std::string) TODO \
    OWNED_LIST_FIELD(X, Y, TEMPLATE_PEER_SESSION, BgpNeighborSessionRegistry, std::string) TODO

DEFINE_CONFIG_GROUP(Bgp, BGP_FIELD_LIST)

}

#endif // BGP_REGISTRY_H

