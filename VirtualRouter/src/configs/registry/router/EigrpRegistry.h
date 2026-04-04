/**
 * @file EigrpRegistry.h
 * @brief EIGRP configuration registry: process, AF, and interface settings.
 *
 * Defines the configuration schema for EIGRP including process parameters,
 * address-family configuration, metric tuning, stub mode, and per-interface
 * EIGRP settings (bandwidth, delay, reliability, timers).
 */

#ifndef EIGRP_REGISTRY_H
#define EIGRP_REGISTRY_H

#include <string>
#include <IPAddress.h>

#include "configs/RegistryTypes.hpp"
#include "configs/RegistryDefaultTable.hpp"
#include "EigrpInterfaceRegistry.h"
#include "configs/SubRegistry.hpp"
#include "interface/configs/InterfaceType.hpp"

namespace config
{
namespace eigrp
{

enum class TrafficShareMode
{
    BALENCED, ///< Balanced traffic sharing.
    MINIMUM,   ///< Minimum traffic sharing.
    MINIMUM_ACROSS_INTERFACE ///< TODO: will still do ecmp but with variance of 0
};

enum class RerouteTieBreak
{
    INTERFACE_DISJOINT,
    LINECARD_DISJOINT,
    LOWEST_BACKUP_PATH_METRIC,
    SRLG_DISJOINT
};

} // namespace config::eigrp

enum class Eigrp
{
    AUTO_SUMMARIZATION,
    AF_INTERFACE,
    BFD_ALL_INTERFACE,
    BFD_INTERFACE,
    DEFAULT_INFORMATION_IN,
    DEFAULT_INFORMATION_OUT,
    DEFAULT_METRICS,
    ADMIN_DISTANCE_RANGES,
    INTERNAL_ADMIN_DISTANCE,
    EXTERNAL_ADMIN_DISTANCE,
    DISTRIBUTE_LIST_IN, // TODO
    DISTRIBUTE_LIST_IN_INTERFACE,// TODO
    DISTRIBUTE_LIST_IN_ACL, // TODO bool
    DISTRIBUTE_LIST_IN_PREFIX,// TODO bool
    DISTRIBUTE_LIST_IN_GATEWAY,// TODO bool
    DISTRIBUTE_LIST_OUT,// TODO
    DISTRIBUTE_LIST_OUT_INTERFACE,// TODO
    DISTRIBUTE_LIST_OUT_ACL,// TODO bool
    DISTRIBUTE_LIST_OUT_PREFIX, // TODO bool
    DISTRIBUTE_LIST_OUT_GATEWAY, // TODO bool
    MAX_EVENT_LOG_SIZE,
    LOG_NEIGHBOR_CHANGES,
    LOG_NEIGHBOR_WARNINGS,
    LOG_NEIGHBOR_WARNINGS_INTERVAL,
    ROUTER_ID,
    STUB,
    STUB_CONNECTED,
    STUB_RECEIVE_ONLY,
    STUB_REDISTRIBUTED,
    STUB_STATIC,
    STUB_SUMMARY,
    STUB_LEAK_MAP,
    FAST_REROUTE_LOAD_SHARING,
    FAST_REROUTE_PER_PREFIX_ALL,
    FAST_REROUTE_PER_PREFIX_ROUTE_MAP,
    FAST_REROUTE_TIE_BREAK_INTERFACE_DISJOINT,
    FAST_REROUTE_TIE_BREAK_LINECARD_DISJOINT,
    FAST_REROUTE_TIE_BREAK_LOWEST_BACKUP_PATH_METRIC,
    FAST_REROUTE_TIE_BREAK_SRLG_DISJOINT,
    MAX_PATHS,
    MAX_HOPS,
    WEIGHT_TOS,
    WEIGTH_K1,
    WEIGHT_K2,
    WEIGHT_K3,
    WEIGHT_k4,
    WEIGHT_k5,
    NEIGHBOR,                   // types::IPAddress, uint32(interface)
    NETWORK,                    // uint32(address), uint32(wildcard)
    OFFSET_LIST_IN,             // string
    OFFSET_LIST_IN_OFFSET,      // uint32
    OFFSET_LIST_IN_INTERFACE,   // uint32
    OFFSET_LIST_OUT,
    OFFSET_LIST_OUT_OFFSET,
    OFFSET_LIST_OUT_INTERFACE,
    PASSIVE_INTERFACES,
    //REDISTRIBUTE,               // TODO
    SHUTDOWN,
    SUMMARY_METRIC,             // types::IPAddress, uint8(mask), uint32(bw), uint32(delay), uint8(reliability), uint8(load), uint16(mtu), uint8(distance)
    ACTIVE_TIME,                // uint16 optional
    ACTIVE_DISABLED,            // bool
    GRACEFUL_PURGE_TIME,        // uint16
    NON_STOP_FORWARDING,        // bool
    WIDE_METRIC,                // uint32
    RIB_SCALE,                  // uint8
    MAXIMUM_PREFIX,             // uint32 (0 = disabled)
    DAMPENING,                  // bool
    DAMPENING_WARNINGS,         // bool
    DAMPENING_INTERVAL,         // uint8 (process-level)
    DAMPENING_RESET_TIME,       // uint16
    DAMPENING_RESTART,          // uint16
    DAMPENING_RESTART_COUNT,    // uint16
    TRAFFIC_SHARE,
    VARIANCE,
    COUNT
};

#define EIGRP_DEFAULTS(X) \
    X(Eigrp, AUTO_SUMMARIZATION, false) \
    X(Eigrp, BFD_ALL_INTERFACE, false) \
    X(Eigrp, DISTRIBUTE_LIST_IN_ACL, false) \
    X(Eigrp, DISTRIBUTE_LIST_IN_PREFIX, false) \
    X(Eigrp, DISTRIBUTE_LIST_IN_GATEWAY, false) \
    X(Eigrp, DISTRIBUTE_LIST_OUT_ACL, false) \
    X(Eigrp, DISTRIBUTE_LIST_OUT_PREFIX, false) \
    X(Eigrp, DISTRIBUTE_LIST_OUT_GATEWAY, false) \
    X(Eigrp, MAX_EVENT_LOG_SIZE, 500) \
    X(Eigrp, LOG_NEIGHBOR_CHANGES, true) \
    X(Eigrp, LOG_NEIGHBOR_WARNINGS, false) \
    X(Eigrp, LOG_NEIGHBOR_WARNINGS_INTERVAL, 10) \
    X(Eigrp, INTERNAL_ADMIN_DISTANCE, 90) \
    X(Eigrp, EXTERNAL_ADMIN_DISTANCE, 170) \
    X(Eigrp, STUB, false) \
    X(Eigrp, STUB_CONNECTED, true) \
    X(Eigrp, STUB_RECEIVE_ONLY, false) \
    X(Eigrp, STUB_REDISTRIBUTED, true) \
    X(Eigrp, STUB_STATIC, true) \
    X(Eigrp, STUB_SUMMARY, true) \
    X(Eigrp, FAST_REROUTE_LOAD_SHARING, false) \
    X(Eigrp, FAST_REROUTE_PER_PREFIX_ALL, false) \
    X(Eigrp, MAX_PATHS, 4) \
    X(Eigrp, MAX_HOPS, 100) \
    X(Eigrp, WEIGHT_TOS, 0) \
    X(Eigrp, WEIGTH_K1, 1) \
    X(Eigrp, WEIGHT_K2, 0) \
    X(Eigrp, WEIGHT_K3, 1) \
    X(Eigrp, WEIGHT_k4, 0) \
    X(Eigrp, WEIGHT_k5, 0) \
    X(Eigrp, SHUTDOWN, false) \
    X(Eigrp, ACTIVE_DISABLED, false) \
    X(Eigrp, GRACEFUL_PURGE_TIME, 240) \
    X(Eigrp, NON_STOP_FORWARDING, false) \
    X(Eigrp, WIDE_METRIC, 10000000) \
    X(Eigrp, RIB_SCALE, 128) \
    X(Eigrp, MAXIMUM_PREFIX, 0) \
    X(Eigrp, DAMPENING, false) \
    X(Eigrp, DAMPENING_WARNINGS, false) \
    X(Eigrp, DAMPENING_INTERVAL, 75) \
    X(Eigrp, DAMPENING_RESET_TIME, 0) \
    X(Eigrp, DAMPENING_RESTART, 0) \
    X(Eigrp, DAMPENING_RESTART_COUNT, 1) \
    X(Eigrp, TRAFFIC_SHARE, eigrp::TrafficShareMode::BALENCED) \
    X(Eigrp, VARIANCE, 1)

CONFIG_DEFAULT_TABLE(EIGRP_DEFAULTS);

void EigrpSyncNetworks(void* e);
void EigrpShutdown(void* e);
void EigrpSyncVariance(void* e);
void EigrpSyncKValues(void* e);
void EigrpSyncNeighbors(void* e);
void EigrpSyncPassive(void* e);
void EigrpSyncRouterId(void* e);

using EigrpRegistry = SubRegistry<Eigrp,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::AUTO_SUMMARIZATION)>,
    OwnedListField<config::EigrpInterfaceRegistry, interface::InterfaceKey CONFIG_INDEX_ARG(routing::eigrp::AF_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::BFD_ALL_INTERFACE)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(routing::eigrp::BFD_INTERFACE)>,
    ValueField<std::string CONFIG_INDEX_ARG(routing::eigrp::DEFAULT_INFORMATION_IN)>,
    ValueField<std::string CONFIG_INDEX_ARG(routing::eigrp::DEFAULT_INFORMATION_OUT)>,
    ValueField<std::tuple<uint32_t, uint32_t, uint8_t, uint8_t, uint16_t> CONFIG_INDEX_ARG(routing::eigrp::DEFAULT_METRICS)>,
    ListField<std::vector<std::tuple<uint8_t, types::IPAddress, types::IPAddress, std::string>> CONFIG_INDEX_ARG(routing::eigrp::ADMIN_DISTANCE_RANGES)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::INTERNAL_ADMIN_DISTANCE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::EXTERNAL_ADMIN_DISTANCE)>,
    ValueField<std::string CONFIG_INDEX_ARG(routing::eigrp::DISTRIBUTE_LIST_IN)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(routing::eigrp::DISTRIBUTE_LIST_IN_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::DISTRIBUTE_LIST_IN_ACL)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::DISTRIBUTE_LIST_IN_PREFIX)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::DISTRIBUTE_LIST_IN_GATEWAY)>,
    ValueField<std::string CONFIG_INDEX_ARG(routing::eigrp::DISTRIBUTE_LIST_OUT)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(routing::eigrp::DISTRIBUTE_LIST_OUT_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::DISTRIBUTE_LIST_OUT_ACL)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::DISTRIBUTE_LIST_OUT_PREFIX)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::DISTRIBUTE_LIST_OUT_GATEWAY)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(routing::eigrp::MAX_EVENT_LOG_SIZE)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::LOG_NEIGHBOR_CHANGES)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::LOG_NEIGHBOR_WARNINGS)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(routing::eigrp::LOG_NEIGHBOR_WARNINGS_INTERVAL)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(routing::eigrp::ROUTER_ID), EigrpSyncRouterId>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::STUB)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::STUB_CONNECTED)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::STUB_RECEIVE_ONLY)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::STUB_REDISTRIBUTED)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::STUB_STATIC)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::STUB_SUMMARY)>,
    ValueField<std::string CONFIG_INDEX_ARG(routing::eigrp::STUB_LEAK_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::FAST_REROUTE_LOAD_SHARING)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::FAST_REROUTE_PER_PREFIX_ALL)>,
    ValueField<std::string CONFIG_INDEX_ARG(routing::eigrp::FAST_REROUTE_PER_PREFIX_ROUTE_MAP)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::FAST_REROUTE_TIE_BREAK_INTERFACE_DISJOINT)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::FAST_REROUTE_TIE_BREAK_LINECARD_DISJOINT)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::FAST_REROUTE_TIE_BREAK_LOWEST_BACKUP_PATH_METRIC)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::FAST_REROUTE_TIE_BREAK_SRLG_DISJOINT)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::MAX_PATHS)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::MAX_HOPS)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::WEIGHT_TOS)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::WEIGTH_K1), EigrpSyncKValues>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::WEIGHT_K2), EigrpSyncKValues>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::WEIGHT_K3), EigrpSyncKValues>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::WEIGHT_k4), EigrpSyncKValues>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::WEIGHT_k5), EigrpSyncKValues>,
    ListField<std::vector<std::tuple<types::IPAddress, interface::InterfaceKey>> CONFIG_INDEX_ARG(routing::eigrp::NEIGHBOR), EigrpSyncNeighbors>,
    ListField<std::vector<std::tuple<uint32_t, uint32_t>> CONFIG_INDEX_ARG(routing::eigrp::NETWORK), EigrpSyncNetworks>,
    ValueField<std::string CONFIG_INDEX_ARG(routing::eigrp::OFFSET_LIST_IN)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(routing::eigrp::OFFSET_LIST_IN_OFFSET)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(routing::eigrp::OFFSET_LIST_IN_INTERFACE)>,
    ValueField<std::string CONFIG_INDEX_ARG(routing::eigrp::OFFSET_LIST_OUT)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(routing::eigrp::OFFSET_LIST_OUT_OFFSET)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(routing::eigrp::OFFSET_LIST_OUT_INTERFACE)>,
    ListField<std::vector<interface::InterfaceKey> CONFIG_INDEX_ARG(routing::eigrp::PASSIVE_INTERFACES), EigrpSyncPassive>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::SHUTDOWN), EigrpShutdown>,
    ListField<std::vector<std::tuple<types::IPAddress, uint8_t, uint32_t, uint32_t, uint8_t, uint8_t, uint16_t, uint8_t>> CONFIG_INDEX_ARG(routing::eigrp::SUMMARY_METRIC)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(routing::eigrp::ACTIVE_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::ACTIVE_DISABLED)>, AtomicField<uint16_t CONFIG_INDEX_ARG(routing::eigrp::GRACEFUL_PURGE_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::NON_STOP_FORWARDING)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(routing::eigrp::WIDE_METRIC)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::RIB_SCALE)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(routing::eigrp::MAXIMUM_PREFIX)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::DAMPENING)>,
    AtomicField<bool CONFIG_INDEX_ARG(routing::eigrp::DAMPENING_WARNINGS)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::DAMPENING_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(routing::eigrp::DAMPENING_RESET_TIME)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(routing::eigrp::DAMPENING_RESTART)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(routing::eigrp::DAMPENING_RESTART_COUNT)>,
    AtomicField<eigrp::TrafficShareMode CONFIG_INDEX_ARG(routing::eigrp::TRAFFIC_SHARE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(routing::eigrp::VARIANCE), EigrpSyncVariance>
>;

}

#endif // EIGRP_REGISTRY_H
