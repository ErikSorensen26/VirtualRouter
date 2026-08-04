/**
 * @file BgpRegistry.h
 * @brief BGP configuration registry: process, neighbor, AF, and transport settings.
 * @ingroup BGP
 *
 * Defines the configuration schema for BGP including process-level parameters,
 * address-family activation and policy, per-neighbor session tuning, peer-group
 * and peer-template inheritance, and transport (keepalive, hold-time) settings.
 */

#ifndef BGP_REGISTRY_H
#define BGP_REGISTRY_H

#include <string>
#include <IPAddress.h>

#include "interface/configs/InterfaceType.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryReference.hpp"
#include "configs/RegistryBuilder.hpp"

namespace config
{
namespace bgp
{

/**
 * @brief Slow-peer detection strategy for a BGP address family.
 * @ingroup BGP
 */
enum class SlowPeerMode
{
    STATIC,           ///< Peer is statically marked as slow.
    DYNAMIC,          ///< Peer is dynamically detected as slow and moved per-update.
    DYNAMIC_PERMANENT ///< Dynamically detected and permanently held in the slow group.
};

}

/**
 * @brief Shared transport parameters inherited by BGP process and neighbor sessions.
 * @ingroup BGP
 */
#define BGP_TRANSPORT_BASE_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, KEEPALIVE_INTERVAL, uint16_t, 60) \
    ATOMIC_FIELD(X, Y, HOLDTIME, uint16_t, 180) \
    OPTIONAL_ATOMIC_FIELD(X, Y, MINIMUM_HOLDTIME, uint16_t) \
    ATOMIC_FIELD(X, Y, TRANSPORT_PATH_MTU_DISCOVERY, bool, false)

DEFINE_CONFIG_GROUP(BgpTransportBase, BGP_TRANSPORT_BASE_FIELD_LIST)

/**
 * @brief Shared address-family parameters inherited by BGP process and neighbor AF configs.
 * @ingroup BGP
 */
#define BGP_AF_BASE_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, ADDITIONAL_PATHS_RECEIVE, bool, false) \
    ATOMIC_FIELD(X, Y, ADDITIONAL_PATHS_SEND, bool, false) \
    ATOMIC_FIELD(X, Y, ADVERTISE_ADDITIONAL_PATHS_ALL, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, ADVERTISE_ADDITIONAL_PATHS_BEST, uint8_t) \
    ATOMIC_FIELD(X, Y, ADVERTISE_ADDITIONAL_GROUP_BEST, bool, false) \
    ATOMIC_FIELD(X, Y, ADVERTISE_BEST_EXTERNAL, bool, false) \
    ATOMIC_FIELD(X, Y, DEFAULT_ORIGINATE, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SLOW_PEER_MODE, config::bgp::SlowPeerMode) \
    ATOMIC_FIELD(X, Y, SLOW_PEER_DETECTION, bool, false) \
    ATOMIC_FIELD(X, Y, SLOW_PEER_DETECTION_THRESHOLD, uint16_t, 300)

DEFINE_CONFIG_GROUP(BgpAfBase, BGP_AF_BASE_FIELD_LIST)


void BgpNeighborDefaultOriginate(void*);

/**
 * @brief Per-neighbor, per-address-family BGP configuration fields.
 * @ingroup BGP
 */
#define BGP_NEIGHBOR_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, AF_BASE, BgpAfBaseRegistry) \
    ATOMIC_FIELD(X, Y, ACTIVATE, bool, false) \
    ATOMIC_FIELD(X, Y, ADVERTISE_DIVERSE_PATH_BACKUP, bool, false) \
    ATOMIC_FIELD(X, Y, ADVERTISE_DIVERSE_PATH_MPATH, bool, false) \
    VALUE_FIELD(X, Y, ADVERTISE_MAP, std::string) TODO \
    VALUE_FIELD(X, Y, ADVERTISE_MAP_EXIST_CONDITION, std::string) TODO \
    VALUE_FIELD(X, Y, ADVERTISE_MAP_NON_EXIST_CONDITION, std::string) TODO \
    ATOMIC_FIELD(X, Y, ADVERTISE_INTERVAL, uint16_t, 30) \
    ATOMIC_FIELD(X, Y, ALLOWAS_IN, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, ALLOWAS_IN_OCCURANCES, uint8_t) \
    ATOMIC_FIELD(X, Y, ANNOUNCE_RPKI_STATE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, ORF_BOTH, bool, false) \
    ATOMIC_FIELD(X, Y, ORF_RECEIVE, bool, false) \
    ATOMIC_FIELD(X, Y, ORF_SEND, bool, false) \
    VALUE_FIELD(X, Y, ORIGINATE_ROUTE_MAP, std::string) TODO \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_IN, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_IN_INTERFACE, interface::InterfaceKey) TODO \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_OUT, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_OUT_INTERFACE, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, DMZLINK_BW, bool, false) TODO \
    VALUE_FIELD(X, Y, FILTER_LIST_IN, std::string) TODO \
    VALUE_FIELD(X, Y, FILTER_LIST_OUT, std::string) TODO \
    VALUE_FIELD(X, Y, INHERIT_PEER_POLICY, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, MAXIMUM_PREFIX, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, MAXIMUM_PREFIX_THRESHOLD, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, MAXIMUM_PREFIX_RESTART, uint16_t) \
    ATOMIC_FIELD(X, Y, MAXIMUM_PREFIX_WARNING_ONLY, bool, false) \
    ATOMIC_FIELD(X, Y, NEXT_HOP_SELF, bool, false) \
    ATOMIC_FIELD(X, Y, NEXT_HOP_SELF_ALL, bool, false) \
    ATOMIC_FIELD(X, Y, NEXT_HOP_UNCHANGED, bool, false) \
    VALUE_FIELD(X, Y, PREFIX_LIST_IN, std::string) TODO \
    VALUE_FIELD(X, Y, PREFIX_LIST_OUT, std::string) TODO \
    ATOMIC_FIELD(X, Y, REMOVE_PRIVATE_AS, bool, false) \
    ATOMIC_FIELD(X, Y, REMOVE_PRIVATE_AS_ALL, bool, false) \
    VALUE_FIELD(X, Y, ROUTE_MAP_IN, std::string) TODO \
    VALUE_FIELD(X, Y, ROUTE_MAP_OUT, std::string) TODO \
    ATOMIC_FIELD(X, Y, ROUTE_REFLECTOR_CLIENT, bool, false) \
    ATOMIC_FIELD(X, Y, ROUTE_SERVER_CLIENT, bool, false) TODO \
    VALUE_FIELD(X, Y, ROUTE_SERVER_CLIENT_CONTEXT, std::string) TODO \
    ATOMIC_FIELD(X, Y, SEND_COMMUNITY, bool, false) \
    ATOMIC_FIELD(X, Y, SEND_COMMUNITY_BOTH, bool, false) \
    ATOMIC_FIELD(X, Y, SEND_COMMUNITY_EXTENDED, bool, false) \
    ATOMIC_FIELD(X, Y, SEND_COMMUNITY_STANDARD, bool, false) \
    ATOMIC_FIELD(X, Y, SOFT_RECONFIGURATION, bool, false) \
    ATOMIC_FIELD(X, Y, TRANSLATE_UPDATE, bool, false) TODO \
    VALUE_FIELD(X, Y, UNSUPPRESS_MAP, std::string) TODO \
    VALUE_FIELD(X, Y, WEIGHT, uint16_t)

DEFINE_CONFIG_GROUP(BgpNeighbor, BGP_NEIGHBOR_FIELD_LIST)


void BgpNeighborSessionShutdown(void*);
void BgpNeighborSessionPathAttribute(void*);


#define BGP_PATH_ATTRIBUTE_FIELDS(X) \
    X(bool,    discard) \
    X(uint8_t, start) \
    X(uint8_t, end)

DEFINE_TUPLE_SCHEMA(BgpPathAttribute, BGP_PATH_ATTRIBUTE_FIELDS);

/**
 * @brief Session-level BGP neighbor configuration fields (transport, timers, auth, path attributes).
 * @ingroup BGP
 */
#define BGP_NEIGHBOR_SESSION_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, BGP_BASE, BgpTransportBaseRegistry) \
    VALUE_FIELD(X, Y, DESCRIPTION, std::string) TODO \
    ATOMIC_FIELD(X, Y, DISABLE_CONNECTION_CHECK, bool, false) \
    ATOMIC_FIELD(X, Y, EBGP_MULTIHOP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, EBGP_MAX_HOP_COUNT, uint8_t, 1) TODO \
    ATOMIC_FIELD(X, Y, FALL_OVER, bool, false) TODO \
    ATOMIC_FIELD(X, Y, FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, FALL_OVER_BFD_MULTI_HOP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, FALL_OVER_BFD_SINGLE_HOP, bool, false) TODO \
    VALUE_FIELD(X, Y, FALL_OVER_ROUTE_MAP, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, HAMODE_GRACEFUL_RESTART, bool) TODO \
    VALUE_FIELD(X, Y, INHERIT_PEER_SESSION, std::string) \
    ATOMIC_FIELD(X, Y, LOCAL_AS, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, LOCAL_AS_AS, uint32_t) \
    ATOMIC_FIELD(X, Y, LOCAL_AS_NO_PREPEND, bool, false) \
    ATOMIC_FIELD(X, Y, LOCAL_AS_REPLACE_AS, bool, false) \
    ATOMIC_FIELD(X, Y, LOCAL_AS_DUAL_AS, bool, false) \
    VALUE_FIELD(X, Y, PASSWORD, std::string) TODO \
    LIST_FIELD_CB(X, Y, PATH_ATTRIBUTE, BgpPathAttribute::Tuple, BgpNeighborSessionPathAttribute) \
    VALUE_FIELD(X, Y, PEER_GROUP, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, REMOTE_AS, uint32_t) \
    ATOMIC_FIELD_CB(X, Y, SHUTDOWN, bool, false, BgpNeighborSessionShutdown) \
    OPTIONAL_ATOMIC_FIELD(X, Y, TRANSPORT_CONNECTION_MODE, bool) \
    ATOMIC_FIELD(X, Y, TRANSPORT_MULTI_SESSION, bool, false) \
    ATOMIC_FIELD(X, Y, TTL_SEC, bool, false) TODO \
    ATOMIC_FIELD(X, Y, TTL_SEC_HOP, uint8_t, 1) TODO \
    OWNED_LIST_FIELD(X, Y, AF_NEIGHBOR, BgpNeighborRegistry, uint32_t)

DEFINE_CONFIG_GROUP(BgpNeighborSession, BGP_NEIGHBOR_SESSION_FIELD_LIST)

TUPLE_SCHEMA_FOR(BgpNeighborSession, BgpNeighborSession::PATH_ATTRIBUTE, BgpPathAttribute);


#define BGP_AGGREGATE_ADDRESS_FIELDS(X) \
    X(types::IPPrefix,    prefix) \
    X(std::string, advertiseMap) \
    X(bool,        asConfedSet) \
    X(std::string, attributeMap) \
    X(std::string, routeMap) \
    X(bool,        summaryOnly) \
    X(std::string, suppressMap)

DEFINE_TUPLE_SCHEMA(BgpAggregateAddress, BGP_AGGREGATE_ADDRESS_FIELDS);


// Aliased because IGNOR() is fixed arity and cannot absorb the type's commas.
using BgpDistancePrefixes = std::vector<std::tuple<types::IPPrefix, std::string>>;

#define BGP_DISTANCE_RANGE_FIELDS(X) \
    X(uint8_t, distance) \
    X(IGNOR(BgpDistancePrefixes), prefixes)

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
    LIST_FIELD(X, Y, AGGREGATE_ADDRESS, BgpAggregateAddress::Tuple) \
    ATOMIC_FIELD(X, Y, BGP_ADDITIONAL_PATHS_INSTALL, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_ADDITIONAL_PATHS_SELECT, uint8_t) TODO \
    ATOMIC_FIELD(X, Y, BGP_ADDITIONAL_PATHS_SELECT_BACKUP, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_AGGREGATE_TIMER, uint16_t, 30) \
    ATOMIC_FIELD(X, Y, BGP_BEST_PATH_COMPARE_ROUTER_ID, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_BEST_PATH_COST_COMMUNITY_IGNORE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_BEST_PATH_IGP_METRIC_IGNORE, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_BEST_PATH_MED_CONFED, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_BEST_PATH_MED_MISSING_AS_WORST, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_DAMPENING, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_DAMPENING_HALF_LIFE, uint8_t, 15) \
    ATOMIC_FIELD(X, Y, BGP_DAMPENING_REUSE_THRESHOLD, uint16_t, 750) \
    ATOMIC_FIELD(X, Y, BGP_DAMPENING_SUPPRESS_THRESHOLD, uint16_t, 2000) \
    ATOMIC_FIELD(X, Y, BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME, uint8_t, 60) \
    VALUE_FIELD(X, Y, BGP_DAMPENING_ROUTE_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, BGP_DMZLINK_BW, bool, false) TODO \
    VALUE_FIELD(X, Y, BGP_INJECT_MAP, std::string) TODO \
    VALUE_FIELD(X, Y, BGP_INJECT_MAP_EXIST_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, BGP_INJECT_MAP_COPY_ATTRIBUTES, bool, false) TODO \
    VALUE_FIELD(X, Y, BGP_NEXT_HOP_ROUTE_MAP, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_NEXT_HOP_TRIGGER_DELAY, uint16_t) \
    ATOMIC_FIELD(X, Y, BGP_NEXT_HOP_TRACKING, bool, true) \
    ATOMIC_FIELD(X, Y, BGP_RECURSIVE_HOST, bool, true) \
    ATOMIC_FIELD(X, Y, BGP_REDISTRIBUTE_INTERNAL, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_ROUTE_MAP_PRIORITY, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_SOFT_RECONFIG_BACKUP, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DEFAULT_METRIC, uint32_t) \
    LIST_FIELD(X, Y, DISTANCE_RANGE, BgpDistanceRange::Tuple) \
    ATOMIC_FIELD(X, Y, DISTANCE_BGP_EXTERNAL, uint8_t, 20) \
    ATOMIC_FIELD(X, Y, DISTANCE_BGP_INTERNAL, uint8_t, 200) \
    ATOMIC_FIELD(X, Y, DISTANCE_BGP_LOCAL, uint8_t, 200) \
    ATOMIC_FIELD(X, Y, DISTANCE_MBGP_EXTERNAL, uint8_t, 20) \
    ATOMIC_FIELD(X, Y, DISTANCE_MBGP_INTERNAL, uint8_t, 200) \
    ATOMIC_FIELD(X, Y, DISTANCE_MBGP_LOCAL, uint8_t, 200) \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_IN, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_IN_INTERFACE, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_IN_PREFIX, bool, false) TODO \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_OUT, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_OUT_INTERFACE, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_OUT_PREFIX, bool, false) TODO \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_GATEWAY, std::string) TODO \
    ATOMIC_FIELD(X, Y, MAXIMUM_PATHS_EBGP, uint8_t, 1) \
    ATOMIC_FIELD(X, Y, MAXIMUM_PATHS_IBGP, uint8_t, 1) \
    LIST_FIELD(X, Y, NETWORK, BgpNetwork::Tuple) \
    VALUE_FIELD(X, Y, TABLE_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, TABLE_MAP_FILTER, bool, false) TODO

DEFINE_CONFIG_GROUP(BgpAddressFamily, BGP_ADDRESS_FAMILY_FIELD_LIST)

TUPLE_SCHEMA_FOR(BgpAddressFamily, BgpAddressFamily::AGGREGATE_ADDRESS, BgpAggregateAddress);
TUPLE_SCHEMA_FOR(BgpAddressFamily, BgpAddressFamily::DISTANCE_RANGE, BgpDistanceRange);
TUPLE_SCHEMA_FOR(BgpAddressFamily, BgpAddressFamily::NETWORK, BgpNetwork);



#define BGP_LISTEN_RANGE_FIELDS(X) \
    X(uint32_t,    address) \
    X(uint32_t,    prefixLength) \
    X(std::string, peerGroup)

DEFINE_TUPLE_SCHEMA(BgpListenRange, BGP_LISTEN_RANGE_FIELDS);

#define BGP_RPKI_SERVER_FIELDS(X) \
    X(types::IPAddress, address) \
    X(uint16_t,         port) \
    X(uint16_t,         refreshTime) \
    X(std::string,      sshUsername) \
    X(std::string,      sshPassword)

DEFINE_TUPLE_SCHEMA(BgpRpkiServer, BGP_RPKI_SERVER_FIELDS);

/**
 * @brief Top-level BGP process configuration fields.
 * @ingroup BGP
 */
#define BGP_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, AUTONOMOUS_SYSTEM, uint32_t, 0) \
    REGISTRY_CONTAINER(X, Y, BGP_BASE, BgpTransportBaseRegistry) \
    OWNED_LIST_FIELD(X, Y, ADDRESS_FAMILIES, BgpAddressFamilyRegistry, uint32_t) \
    ATOMIC_FIELD(X, Y, BGP_ALWAYS_COMPARE_MED, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_AS_DOT_NOTATION, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_CLIENT_TO_CLIENT_REFLECTION, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_CLUSTER_ID, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_CONFEDERATION_IDENTIFIER, uint32_t) \
    LIST_FIELD(X, Y, BGP_CONFEDERATION_PEERS, uint32_t) \
    ATOMIC_FIELD(X, Y, BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL, uint32_t, 60) TODO \
    ATOMIC_FIELD(X, Y, BGP_DETERMINISTIC_MED, bool, false) \
    ATOMIC_FIELD(X, Y, BGP_DMZLINK_BW, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_ENFORCE_FIRST_AS, bool, true) \
    ATOMIC_FIELD(X, Y, BGP_ENHANCED_ERROR, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_FAST_EXTERNAL_FAILOVER, bool, true) TODO \
    ATOMIC_FIELD(X, Y, BGP_GRACEFUL_RESTART, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_GRACEFUL_RESTART_EXTENDED, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_GRACEFUL_RESTART_RESTART_TIME, uint16_t, 120) TODO \
    ATOMIC_FIELD(X, Y, BGP_GRACEFUL_RESTART_STALEPATH_TIME, uint16_t, 360) TODO \
    VALUE_FIELD(X, Y, BGP_INJECT_MAP, std::string) TODO \
    VALUE_FIELD(X, Y, BGP_INJECT_MAP_EXIST_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, BGP_INJECT_MAP_COPY_ATTRIBUTES, bool, false) TODO \
    ATOMIC_FIELD(X, Y, BGP_LISTEN, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_LISTEN_LIMIT, uint16_t) \
    LIST_FIELD(X, Y, BGP_LISTEN_RANGE, BgpListenRange::Tuple) \
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
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_ROUTER_ID, uint32_t) \
    LIST_FIELD(X, Y, BGP_RPKI_SERVER, BgpRpkiServer::Tuple) TODO \
    ATOMIC_FIELD(X, Y, BGP_SCAN_TIME, uint8_t, 60) \
    ATOMIC_FIELD(X, Y, BGP_SUPPRESS_INACTIVE, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, BGP_UPDATE_DELAY, uint16_t) \
    OWNED_LIST_FIELD(X, Y, NEIGHBOR, BgpNeighborSessionRegistry, types::IPAddress) \
    OWNED_LIST_FIELD(X, Y, PEER_GROUP, BgpNeighborSessionRegistry, std::string) \
    VALUE_FIELD(X, Y, ROUTE_SERVER_CONTEXT, std::string) TODO \
    OWNED_LIST_FIELD(X, Y, TEMPLATE_PEER_POLICY, BgpNeighborRegistry, std::string) TODO \
    OWNED_LIST_FIELD(X, Y, TEMPLATE_PEER_SESSION, BgpNeighborSessionRegistry, std::string) TODO

DEFINE_CONFIG_GROUP(Bgp, BGP_FIELD_LIST)

TUPLE_SCHEMA_FOR(Bgp, Bgp::BGP_LISTEN_RANGE, BgpListenRange);

}

#endif // BGP_REGISTRY_H

