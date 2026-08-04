/**
 * @file EigrpRegistry.h
 * @brief EIGRP configuration registry: process, AF, and interface settings.
 * @ingroup EIGRP
 *
 * Defines the configuration schema for EIGRP including process parameters,
 * address-family configuration, metric tuning, stub mode, and per-interface
 * EIGRP settings (bandwidth, delay, reliability, timers).
 */

#ifndef EIGRP_REGISTRY_H
#define EIGRP_REGISTRY_H
#include <string>
#include <IPAddress.h>
#include <EnumBitMap.hpp>

#include "configs/RegistryTypes.hpp"
#include "configs/RegistryBuilder.hpp"
#include "configs/TupleSchema.hpp"
#include "interface/configs/InterfaceType.hpp"
#include "EigrpInterfaceRegistry.h"

namespace config
{
namespace eigrp
{

/**
 * @brief Traffic sharing strategy across equal-cost EIGRP paths.
 * @ingroup EIGRP
 */
enum class TrafficShareMode : uint8_t
{
    BALENCED, ///< Balanced traffic sharing.
    MINIMUM,  ///< Minimum traffic sharing.
};

/**
 * @brief Tie-breaking criteria for EIGRP LFA reroute path selection.
 * @ingroup EIGRP
 */
enum class RerouteTieBreak
{
    INTERFACE_DISJOINT,
    LINECARD_DISJOINT,
    LOWEST_BACKUP_PATH_METRIC,
    SRLG_DISJOINT
};

/**
 * @brief EIGRP stub router advertisement flags.
 * @ingroup EIGRP
 */
enum Stub
{
    CONNECTED,
    RECEIVE_ONLY,
    REDISTRIBUTED,
    STATIC,
    SUMMARY,
    COUNT
};

} // namespace config::eigrp


void EigrpSyncNetworks(void* e);
void EigrpShutdown(void* e);
void EigrpSyncVariance(void* e);
void EigrpSyncKValues(void* e);
void EigrpSyncNeighbors(void* e);
void EigrpSyncPassive(void* e);
void EigrpSyncRouterId(void* e);
void EigrpSyncAfInterface(void* e);

#define EIGRP_DEFAULT_METRICS_FIELDS(X) \
    X(uint32_t, bandwidth) \
    X(uint32_t, delay) \
    X(uint8_t,  reliability) \
    X(uint8_t,  load) \
    X(uint16_t, mtu)

DEFINE_TUPLE_SCHEMA(EigrpDefaultMetrics, EIGRP_DEFAULT_METRICS_FIELDS);

#define EIGRP_ADMIN_DISTANCE_RANGE_FIELDS(X) \
    X(uint8_t,           distance) \
    X(types::IPAddress,  address) \
    X(types::IPAddress,  wildcard) \
    X(std::string,       accessList)

DEFINE_TUPLE_SCHEMA(EigrpAdminDistanceRange, EIGRP_ADMIN_DISTANCE_RANGE_FIELDS);

#define EIGRP_NEIGHBOR_FIELDS(X) \
    X(types::IPAddress,           address) \
    X(interface::InterfaceKey,    iface)

DEFINE_TUPLE_SCHEMA(EigrpNeighbor, EIGRP_NEIGHBOR_FIELDS);

#define EIGRP_NETWORK_FIELDS(X) \
    X(types::IPAddress,  address) \
    X(IGNOR(uint8_t),    prefixLength)

DEFINE_TUPLE_SCHEMA(EigrpNetwork, EIGRP_NETWORK_FIELDS);

#define EIGRP_SUMMARY_METRIC_FIELDS(X) \
    X(types::IPAddress, address) \
    X(uint8_t,          prefixLength) \
    X(uint32_t,         bandwidth) \
    X(uint32_t,         delay) \
    X(uint8_t,          reliability) \
    X(uint8_t,          load) \
    X(uint16_t,         mtu) \
    X(uint8_t,          distance)

DEFINE_TUPLE_SCHEMA(EigrpSummaryMetric, EIGRP_SUMMARY_METRIC_FIELDS);

/**
 * @brief EIGRP process-level configuration fields.
 * @ingroup EIGRP
 */
#define EIGRP_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, IS_NAMED, bool, false) \
    ATOMIC_FIELD(X, Y, AUTO_SUMMARIZATION, bool, false) \
    OWNED_LIST_FIELD(X, Y, AF_INTERFACE, config::EigrpInterfaceRegistry, interface::InterfaceKey) \
    ATOMIC_FIELD(X, Y, BFD_ALL_INTERFACE, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, BFD_INTERFACE, interface::InterfaceKey) \
    VALUE_FIELD(X, Y, DEFAULT_INFORMATION_IN, std::string) \
    VALUE_FIELD(X, Y, DEFAULT_INFORMATION_OUT, std::string) \
    VALUE_FIELD(X, Y, DEFAULT_METRICS, EigrpDefaultMetrics::Tuple) \
    LIST_FIELD(X, Y, ADMIN_DISTANCE_RANGES, EigrpAdminDistanceRange::Tuple) \
    ATOMIC_FIELD(X, Y, INTERNAL_ADMIN_DISTANCE, uint8_t, 90) \
    ATOMIC_FIELD(X, Y, EXTERNAL_ADMIN_DISTANCE, uint8_t, 170) \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_IN, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_IN_INTERFACE, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_IN_ACL, bool, false) TODO \
    ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_IN_PREFIX, bool, false) TODO \
    ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_IN_GATEWAY, bool, false) TODO \
    VALUE_FIELD(X, Y, DISTRIBUTE_LIST_OUT, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_OUT_INTERFACE, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_OUT_ACL, bool, false) TODO \
    ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_OUT_PREFIX, bool, false) TODO \
    ATOMIC_FIELD(X, Y, DISTRIBUTE_LIST_OUT_GATEWAY, bool, false) TODO \
    ATOMIC_FIELD(X, Y, MAX_EVENT_LOG_SIZE, uint32_t, 500) \
    OPTIONAL_ATOMIC_FIELD(X, Y, DEFAULT_ROUTE_TAG, uint32_t) TODO /*implement*/ \
    ATOMIC_FIELD(X, Y, LOG_NEIGHBOR_CHANGES, bool, true) \
    ATOMIC_FIELD(X, Y, LOG_NEIGHBOR_WARNINGS, bool, false) \
    ATOMIC_FIELD(X, Y, LOG_NEIGHBOR_WARNINGS_INTERVAL, uint16_t, 10) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, ROUTER_ID, uint32_t, EigrpSyncRouterId) \
    OPTIONAL_ATOMIC_FIELD(X, Y, STUB, types::EnumBitMap<eigrp::Stub>::type) \
    VALUE_FIELD(X, Y, STUB_LEAK_MAP, std::string) \
    ATOMIC_FIELD(X, Y, FAST_REROUTE_LOAD_SHARING, bool, false) \
    ATOMIC_FIELD(X, Y, FAST_REROUTE_PER_PREFIX_ALL, bool, false) \
    VALUE_FIELD(X, Y, FAST_REROUTE_PER_PREFIX_ROUTE_MAP, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, FAST_REROUTE_TIE_BREAK_INTERFACE_DISJOINT, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, FAST_REROUTE_TIE_BREAK_LINECARD_DISJOINT, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, FAST_REROUTE_TIE_BREAK_LOWEST_BACKUP_PATH_METRIC, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, FAST_REROUTE_TIE_BREAK_SRLG_DISJOINT, uint8_t) \
    ATOMIC_FIELD(X, Y, MAX_PATHS, uint8_t, 4) \
    ATOMIC_FIELD(X, Y, MAX_HOPS, uint8_t, 100) \
    ATOMIC_FIELD_CB(X, Y, WEIGHT_K1, uint8_t, 1, EigrpSyncKValues) \
    ATOMIC_FIELD_CB(X, Y, WEIGHT_K2, uint8_t, 0, EigrpSyncKValues) \
    ATOMIC_FIELD_CB(X, Y, WEIGHT_K3, uint8_t, 1, EigrpSyncKValues) \
    ATOMIC_FIELD_CB(X, Y, WEIGHT_K4, uint8_t, 0, EigrpSyncKValues) \
    ATOMIC_FIELD_CB(X, Y, WEIGHT_K5, uint8_t, 0, EigrpSyncKValues) \
    ATOMIC_FIELD_CB(X, Y, WEIGHT_K6, uint8_t, 0, EigrpSyncKValues) \
    LIST_FIELD_CB(X, Y, NEIGHBOR, EigrpNeighbor::Tuple, EigrpSyncNeighbors) \
    LIST_FIELD_CB(X, Y, NETWORK, EigrpNetwork::Tuple, EigrpSyncNetworks) \
    VALUE_FIELD(X, Y, OFFSET_LIST_IN, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, OFFSET_LIST_IN_OFFSET, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, OFFSET_LIST_IN_INTERFACE, interface::InterfaceKey) \
    VALUE_FIELD(X, Y, OFFSET_LIST_OUT, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, OFFSET_LIST_OUT_OFFSET, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, OFFSET_LIST_OUT_INTERFACE, interface::InterfaceKey) \
    LIST_FIELD_CB(X, Y, PASSIVE_INTERFACES, interface::InterfaceKey, EigrpSyncPassive) \
    ATOMIC_FIELD_CB(X, Y, SHUTDOWN, bool, false, EigrpShutdown) \
    LIST_FIELD(X, Y, SUMMARY_METRIC, EigrpSummaryMetric::Tuple) \
    OPTIONAL_ATOMIC_FIELD(X, Y, ACTIVE_TIME, uint16_t) \
    ATOMIC_FIELD(X, Y, ACTIVE_DISABLED, bool, false) \
    ATOMIC_FIELD(X, Y, GRACEFUL_PURGE_TIME, uint16_t, 240) \
    ATOMIC_FIELD(X, Y, NON_STOP_FORWARDING, bool, false) \
    ATOMIC_FIELD(X, Y, WIDE_METRIC, uint32_t, 10000000) \
    ATOMIC_FIELD(X, Y, RIB_SCALE, uint8_t, 128) \
    ATOMIC_FIELD(X, Y, MAXIMUM_PREFIX, uint32_t, 0) \
    ATOMIC_FIELD(X, Y, DAMPENING, bool, false) \
    ATOMIC_FIELD(X, Y, DAMPENING_WARNINGS, bool, false) \
    ATOMIC_FIELD(X, Y, DAMPENING_THRESHOLD, uint8_t, 75) \
    ATOMIC_FIELD(X, Y, DAMPENING_RESET_TIME, uint16_t, 0) \
    ATOMIC_FIELD(X, Y, DAMPENING_RESTART, uint16_t, 0) \
    ATOMIC_FIELD(X, Y, DAMPENING_RESTART_COUNT, uint16_t, 1) \
    ATOMIC_FIELD(X, Y, NEIGHBOR_MAXIMUM_PREFIX, uint32_t, 0) \
    ATOMIC_FIELD(X, Y, NEIGHBOR_DAMPENING, bool, false) \
    ATOMIC_FIELD(X, Y, NEIGHBOR_DAMPENING_WARNINGS, bool, false) \
    ATOMIC_FIELD(X, Y, NEIGHBOR_DAMPENING_THRESHOLD, uint8_t, 75) \
    ATOMIC_FIELD(X, Y, NEIGHBOR_DAMPENING_RESET_TIME, uint16_t, 0) \
    ATOMIC_FIELD(X, Y, NEIGHBOR_DAMPENING_RESTART, uint16_t, 0) \
    ATOMIC_FIELD(X, Y, NEIGHBOR_DAMPENING_RESTART_COUNT, uint16_t, 1) \
    ATOMIC_FIELD(X, Y, TRAFFIC_SHARE, eigrp::TrafficShareMode, eigrp::TrafficShareMode::BALENCED) \
    ATOMIC_FIELD_CB(X, Y, VARIANCE, uint8_t, 1, EigrpSyncVariance)

DEFINE_CONFIG_GROUP(Eigrp, EIGRP_FIELD_LIST)

TUPLE_SCHEMA_FOR(Eigrp, Eigrp::DEFAULT_METRICS, EigrpDefaultMetrics);
TUPLE_SCHEMA_FOR(Eigrp, Eigrp::ADMIN_DISTANCE_RANGES, EigrpAdminDistanceRange);
TUPLE_SCHEMA_FOR(Eigrp, Eigrp::NEIGHBOR, EigrpNeighbor);
TUPLE_SCHEMA_FOR(Eigrp, Eigrp::NETWORK, EigrpNetwork);
TUPLE_SCHEMA_FOR(Eigrp, Eigrp::SUMMARY_METRIC, EigrpSummaryMetric);


/**
 * @brief Named-mode EIGRP container fields (IPv4 and IPv6 AF instances, shutdown).
 * @ingroup EIGRP
 */
#define EIGRP_NAMED_INSTANCE_FIELDS(X) \
    X(uint16_t,    autonomousSystem) \
    X(std::string, vrf)

DEFINE_TUPLE_SCHEMA(EigrpNamedInstance, EIGRP_NAMED_INSTANCE_FIELDS);

#define EIGRP_NAMED_FIELD_LIST(X, Y) \
    LIST_FIELD(X, Y, NAMED_INSTANCES_V4, EigrpNamedInstance::Tuple) \
    LIST_FIELD(X, Y, NAMED_INSTANCES_V6, EigrpNamedInstance::Tuple) \
    ATOMIC_FIELD(X, Y, SHUTDOWN, bool, false)

/**
 * @brief Named-mode EIGRP container fields (IPv4 and IPv6 AF instances, shutdown).
 * @ingroup EIGRP
 */
DEFINE_CONFIG_GROUP(EigrpNamed, EIGRP_NAMED_FIELD_LIST)

TUPLE_SCHEMA_FOR(EigrpNamed, EigrpNamed::NAMED_INSTANCES_V4, EigrpNamedInstance);
TUPLE_SCHEMA_FOR(EigrpNamed, EigrpNamed::NAMED_INSTANCES_V6, EigrpNamedInstance);
}

#endif // EIGRP_REGISTRY_H
