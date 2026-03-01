// BgpRegistry.h

#ifndef BGP_REGISTRY_H
#define BGP_REGISTRY_H

#include <string>

#include "configs/RegistryDefaultTable.hpp"

#include "IPAddress.hpp"
#include "configs/SubRegistry.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryReference.hpp"

namespace Config
{
inline __uint128_t generateBgpKey(uint32_t vrf, uint32_t as, AddressFamily af)
{
    uint8_t addressFamily = af == AddressFamily::NONE ? 0
        : af == AddressFamily::IPv4 ? 1 : 2;

    __uint128_t key = 0;
    key |= __uint128_t(addressFamily) & maskU128Bits(2);
    key |= __uint128_t(vrf) << 8;
    key |= __uint128_t(as) << 40;
    return key;
}

enum class BgpBase
{
    KEEPALIVE_INTERVAL,
    HOLDTIME,
    MINIMUM_HOLDTIME,
    TRANSPORT_PATH_MTU_DISCOVERY,
    COUNT
};

#define BGP_BASE_DEFAULTS(X) \
    X(BgpBase, KEEPALIVE_INTERVAL, 60) \
    X(BgpBase, HOLDTIME, 180) \
    X(BgpBase, TRANSPORT_PATH_MTU_DISCOVERY, false)

CONFIG_DEFAULT_TABLE(BGP_BASE_DEFAULTS);

using BgpBaseRegistry = SubRegistry<__uint128_t, BgpBase,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpBase::KEEPALIVE_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpBase::HOLDTIME)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(BgpBase::MINIMUM_HOLDTIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpBase::TRANSPORT_PATH_MTU_DISCOVERY)>
>;


enum class BgpNeighbor
{
    BGP_BASE,
    ACTIVATE,
    ADDITIONAL_PATHS_RECEIVE,
    ADDITIONAL_PATHS_SEND,
    ADVERTISE_ADDITIONAL_PATHS_ALL,
    ADVERTISE_ADDITIONAL_PATHS_BEST,
    ADVERTISE_ADDITIONAL_GROUP_BEST,
    ADVERTISE_BEST_EXTERNAL,
    ADVERTISE_DIVERSE_PATH_BACKUP,
    ADVERTISE_DIVERSE_PATH_MPATH,
    ADVERTISE_MAP,
    ADVERTISE_MAP_EXIST_CONDITION,
    ADVERTISE_MAP_NON_EXIST_CONDITION,
    ADVERTISE_INTERVAL,
    ALLOWAS_IN,
    ALLOWAS_IN_OCCURANCES,
    ANNOUNCE_RPKI_STATE,
    ORF_BOTH,
    ORF_RECEIVE,
    ORF_SEND,
    ORIGINATE_ROUTE_MAP,
    DESCRIPTION,
    DISABLE_CONNECTION_CHECK,
    DISTRIBUTE_LIST_IN,
    DISTRIBUTE_LIST_IN_INTERFACE,
    DISTRIBUTE_LIST_OUT,
    DISTRIBUTE_LIST_OUT_INTERFACE,
    DMZLINK_BW,
    EBGP_MULTIHOP,
    EBGP_MAX_HOP_COUNT,
    FALL_OVER,
    FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE,
    FALL_OVER_BFD_MULTI_HOP,
    FALL_OVER_BFD_SINGLE_HOP,
    FALL_OVER_ROUTE_MAP,
    FILTER_LIST_IN,
    FILTER_LIST_OUT,
    HAMODE_GRACEFUL_RESTART,
    INHERIT_PEER_POLICY,
    INHERIT_PEER_SESSION,
    LOCAL_AS,
    LOCAL_AS_AS,
    LOCAL_AS_NO_PREPEND,
    LOCAL_AS_REPLACE_AS,
    LOCAL_AS_DUEL_AS,
    MAXIMUM_PREFIX,
    MAXIMUM_PREFIX_THRESHOLD,
    MAXIMUM_PREFIX_RESTART,
    MAXIMUM_PREFIX_WARNING_ONLY,
    NEXT_HOP_SELF,
    NEXT_HOP_SELF_ALL,
    NEXT_HOP_UNCHANGED,
    PASSWORD,
    PATH_ATTRIBUTE,
    PEER_GROUP,
    PREFIX_LIST_IN,
    PREFIX_LIST_OUT,
    REMOTE_AS,
    REMOTE_AS_SHUTDOWN,
    REMOVE_PRIVATE_AS,
    REMOVE_PRIVATE_AS_ALL,
    ROUTE_MAP_IN,
    ROUTE_MAP_OUT,
    ROUTE_REFLECTOR_CLIENT,
    ROUTE_SERVER_CLIENT,
    ROUTE_SERVER_CLIENT_CONTEXT,
    SEND_COMMUNITY,
    SEND_COMMUNITY_BOTH,
    SEND_COMMUNITY_EXTENDED,
    SEND_COMMUNITY_STANDARD,
    SHUTDOWN,
    SLOW_PEER_DETECTION,
    SLOW_PEER_DETECTION_THRESHOLD,
    SLOW_PEER_SPLIT_UPDATE_GROUP_STATIC,
    SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC,
    SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC_PERMANENT,
    SLOW_RECONFIGURATION,
    TRANSLATE_UPDATE,
    TRANSPORT_CONNECTION_MODE,
    TRANSPORT_MULTI_SESSION,
    TTL_SEC,
    TTL_SEC_HOP,
    UNSUPPRESS_MAP,
    WEIGHT,
    COUNT
};

#define BGP_NEIGHBOR_DEFAULTS(X) \
    X(BgpNeighbor, ACTIVATE, false) \
    X(BgpNeighbor, ADDITIONAL_PATHS_RECEIVE, false) \
    X(BgpNeighbor, ADDITIONAL_PATHS_SEND, false) \
    X(BgpNeighbor, ADVERTISE_ADDITIONAL_PATHS_ALL, false) \
    X(BgpNeighbor, ADVERTISE_ADDITIONAL_GROUP_BEST, false) \
    X(BgpNeighbor, ADVERTISE_BEST_EXTERNAL, false) \
    X(BgpNeighbor, ADVERTISE_DIVERSE_PATH_BACKUP, false) \
    X(BgpNeighbor, ADVERTISE_DIVERSE_PATH_MPATH, false) \
    X(BgpNeighbor, ADVERTISE_INTERVAL, 30) \
    X(BgpNeighbor, ALLOWAS_IN, false) \
    X(BgpNeighbor, ANNOUNCE_RPKI_STATE, false) \
    X(BgpNeighbor, ORF_BOTH, false) \
    X(BgpNeighbor, ORF_RECEIVE, false) \
    X(BgpNeighbor, ORF_SEND, false) \
    X(BgpNeighbor, DISABLE_CONNECTION_CHECK, false) \
    X(BgpNeighbor, DMZLINK_BW, false) \
    X(BgpNeighbor, EBGP_MULTIHOP, false) \
    X(BgpNeighbor, EBGP_MAX_HOP_COUNT, 1) \
    X(BgpNeighbor, FALL_OVER, false) \
    X(BgpNeighbor, FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE, false) \
    X(BgpNeighbor, FALL_OVER_BFD_MULTI_HOP, false) \
    X(BgpNeighbor, FALL_OVER_BFD_SINGLE_HOP, false) \
    X(BgpNeighbor, LOCAL_AS, false) \
    X(BgpNeighbor, LOCAL_AS_NO_PREPEND, false) \
    X(BgpNeighbor, LOCAL_AS_REPLACE_AS, false) \
    X(BgpNeighbor, LOCAL_AS_DUEL_AS, false) \
    X(BgpNeighbor, MAXIMUM_PREFIX_WARNING_ONLY, false) \
    X(BgpNeighbor, NEXT_HOP_SELF, false) \
    X(BgpNeighbor, NEXT_HOP_SELF_ALL, false) \
    X(BgpNeighbor, NEXT_HOP_UNCHANGED, false) \
    X(BgpNeighbor, REMOTE_AS_SHUTDOWN, false) \
    X(BgpNeighbor, REMOVE_PRIVATE_AS, false) \
    X(BgpNeighbor, REMOVE_PRIVATE_AS_ALL, false) \
    X(BgpNeighbor, ROUTE_REFLECTOR_CLIENT, false) \
    X(BgpNeighbor, ROUTE_SERVER_CLIENT, false) \
    X(BgpNeighbor, SEND_COMMUNITY, false) \
    X(BgpNeighbor, SEND_COMMUNITY_BOTH, false) \
    X(BgpNeighbor, SEND_COMMUNITY_EXTENDED, false) \
    X(BgpNeighbor, SEND_COMMUNITY_STANDARD, false) \
    X(BgpNeighbor, SHUTDOWN, false) \
    X(BgpNeighbor, SLOW_PEER_DETECTION, false) \
    X(BgpNeighbor, SLOW_PEER_DETECTION_THRESHOLD, 60) \
    X(BgpNeighbor, SLOW_PEER_SPLIT_UPDATE_GROUP_STATIC, false) \
    X(BgpNeighbor, SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC, false) \
    X(BgpNeighbor, SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC_PERMANENT, false) \
    X(BgpNeighbor, SLOW_RECONFIGURATION, false) \
    X(BgpNeighbor, TRANSLATE_UPDATE, false) \
    X(BgpNeighbor, TRANSPORT_MULTI_SESSION, false) \
    X(BgpNeighbor, TTL_SEC, false) \
    X(BgpNeighbor, TTL_SEC_HOP, 1) \
    X(BgpNeighbor, WEIGHT, 0)

CONFIG_DEFAULT_TABLE(BGP_NEIGHBOR_DEFAULTS);

using BgpNeighborRegistry = SubRegistry<__uint128_t, BgpNeighbor,
    ReferenceContainer<BgpBaseRegistry CONFIG_INDEX_ARG(BgpNeighbor::BGP_BASE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ACTIVATE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ADDITIONAL_PATHS_RECEIVE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ADDITIONAL_PATHS_SEND)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_ADDITIONAL_PATHS_ALL)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_ADDITIONAL_PATHS_BEST)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_ADDITIONAL_GROUP_BEST)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_BEST_EXTERNAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_DIVERSE_PATH_BACKUP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_DIVERSE_PATH_MPATH)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_MAP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_MAP_EXIST_CONDITION)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_MAP_NON_EXIST_CONDITION)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_INTERVAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ALLOWAS_IN)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighbor::ALLOWAS_IN_OCCURANCES)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ANNOUNCE_RPKI_STATE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ORF_BOTH)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ORF_RECEIVE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ORF_SEND)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ORIGINATE_ROUTE_MAP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::DESCRIPTION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::DISABLE_CONNECTION_CHECK)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_IN)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_IN_INTERFACE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_OUT)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_OUT_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::DMZLINK_BW)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::EBGP_MULTIHOP)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighbor::EBGP_MAX_HOP_COUNT)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::FALL_OVER)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::FALL_OVER_BFD_MULTI_HOP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::FALL_OVER_BFD_SINGLE_HOP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::FALL_OVER_ROUTE_MAP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::FILTER_LIST_IN)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::FILTER_LIST_OUT)>,
    OptionalAtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::HAMODE_GRACEFUL_RESTART)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::INHERIT_PEER_POLICY)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::INHERIT_PEER_SESSION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::LOCAL_AS)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighbor::LOCAL_AS_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::LOCAL_AS_NO_PREPEND)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::LOCAL_AS_REPLACE_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::LOCAL_AS_DUEL_AS)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX_THRESHOLD)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX_RESTART)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX_WARNING_ONLY)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::NEXT_HOP_SELF)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::NEXT_HOP_SELF_ALL)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::NEXT_HOP_UNCHANGED)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::PASSWORD)>,
    ValueField<std::vector<std::tuple<
        bool,    // true = discard, false = treat-as-withdraw
        bool,    // isRanged
        uint8_t, // start
        uint8_t  // end
    >> CONFIG_INDEX_ARG(BgpNeighbor::PATH_ATTRIBUTE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::PEER_GROUP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::PREFIX_LIST_IN)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::PREFIX_LIST_OUT)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighbor::REMOTE_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::REMOTE_AS_SHUTDOWN)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::REMOVE_PRIVATE_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::REMOVE_PRIVATE_AS_ALL)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ROUTE_MAP_IN)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ROUTE_MAP_OUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ROUTE_REFLECTOR_CLIENT)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ROUTE_SERVER_CLIENT)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ROUTE_SERVER_CLIENT_CONTEXT)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SEND_COMMUNITY)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SEND_COMMUNITY_BOTH)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SEND_COMMUNITY_EXTENDED)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SEND_COMMUNITY_STANDARD)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SHUTDOWN)>, // ad graceful
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SLOW_PEER_DETECTION)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::SLOW_PEER_DETECTION_THRESHOLD)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SLOW_PEER_SPLIT_UPDATE_GROUP_STATIC)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC)>, // false static
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC_PERMANENT)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SLOW_RECONFIGURATION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::TRANSLATE_UPDATE)>,
    OptionalAtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::TRANSPORT_CONNECTION_MODE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::TRANSPORT_MULTI_SESSION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::TTL_SEC)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighbor::TTL_SEC_HOP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::UNSUPPRESS_MAP)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::WEIGHT)>
>;

enum class Bgp
{
    AGGREGATE_ADDRESS,
    BGP_BASE,
    BGP_ADDITIONAL_PATHS_INSTALL,
    BGP_ADDITIONAL_PATHS_RECEIVE,
    BGP_ADDITIONAL_PATHS_SELECT,
    BGP_ADDITIONAL_PATHS_SELECT_ALL,
    BGP_ADDITIONAL_PATHS_SELECT_BACKUP,
    BGP_ADDITIONAL_PATHS_SELECT_BEST, // uint8
    BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL,
    BGP_ADDITIONAL_PATHS_SELECT_GROUP_BEST,
    BGP_ADVERTISE_BEST_EXTERNAL,
    BGP_AGGREGATE_TIMER,
    BGP_ALWAYS_COMPARE_MED, // DONE
    BGP_AS_DOT_NOTATION,
    BGP_BEST_PATH_COMPARE_ROUTER_ID,
    BGP_BEST_PATH_COST_COMMUNITY_IGNORE,
    BGP_BEST_PATH_IGP_METRIC_IGNORE,
    BGP_BEST_PATH_MED_CONFED,
    BGP_BEST_PATH_MED_MISSING_AS_WORST,
    BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID,
    BGP_CLIENT_TO_CLIENT_REFLECTION,
    BGP_CLUSTER_ID,
    BGP_CONFEDERATION_IDENTIFIER,
    BGP_CONFEDERATION_PEERS,
    BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL,
    BGP_DAMPENING,
    BGP_DAMPENING_HALF_LIFE,
    BGP_DAMPENING_REUSE_THRESHOLD,
    BGP_DAMPENING_SUPPRESS_THRESHOLD,
    BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME,
    BGP_DAMPENING_ROUTE_MAP,
    BGP_DETERMINISTIC_MED,
    BGP_DMZLINK_BW,
    BGP_ENFORCE_FIRST_AS,
    BGP_ENHANCED_ERROR,
    BGP_FAST_EXTERNAL_FAILOVER,
    BGP_GRACEFUL_RESTART,
    BGP_GRACEFUL_RESTART_EXTENDED,
    BGP_GRACEFUL_RESTART_RESTART_TIME,
    BGP_GRACEFUL_RESTART_STALEPATH_TIME,
    BGP_INJECT_MAP,
    BGP_INJECT_MAP_EXIST_MAP,
    BGP_INJECT_MAP_COPY_ATTRIBUTES,
    BGP_LISTEN,
    BGP_LISTEN_LIMIT,
    BGP_LISTEN_RANGE,
    BGP_LOG_NEIGHBOR_CHANGES,
    BGP_MAX_AS_LIMIT,
    BGP_MAX_COMMUNITY_LIMIT,
    BGP_MAX_EXT_COMMUNITY_LIMIT,
    BGP_NEXT_HOP_ROUTE_MAP,
    BGP_NEXT_HOP_TRIGGER_DELAY,
    BGP_NEXT_HOP_TRACKING,
    BGP_NOPEERUP_DELAY_COLD_BOOT,
    BGP_NOPEERUP_DELAY_NSF_SWITCHOVER,
    BGP_NOPEERUP_DELAY_POST_BOOT,
    BGP_NOPEERUP_DELAY_USER_INITIATED,
    BGP_RECURSIVE_HOST,
    BGP_REDISTRIBUTE_INTERNAL,
    BGP_REFRESH_MAX_EOR_TIME,
    BGP_REFRESH_STALEPATH_TIME,
    BGP_REGEX_DETERMINISTIC,
    BGP_ROUTE_MAP_PRIORITY,
    BGP_ROUTER_ID,
    BGP_RPKI_SERVER,
    BGP_SCAN_TIME,
    BGP_SLOW_PEER_DETECTION,
    BGP_SLOW_PEER_DETECTION_THRESHOLD,
    BGP_SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC,
    BGP_SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC_PERMANENT,
    BGP_SOFT_RECONFIG_BACKUP,
    BGP_SUPPRESS_INACTIVE,
    BGP_UPDATE_DELAY,
    DEFAULT_ORIGINATE,
    DEFAULT_METRIC,
    DISTANCE_RANGE,
    DISTANCE_BGP_EXTERNAL,
    DISTANCE_BGP_INTERNAL,
    DISTANCE_BGP_LOCAL,
    DISTANCE_MBGP_EXTERNAL,
    DISTANCE_MBGP_INTERNAL,
    DISTANCE_MBGP_LOCAL,
    DISTRIBUTE_LIST_IN,
    DISTRIBUTE_LIST_IN_INTERFACE,
    DISTRIBUTE_LIST_IN_PREFIX,
    DISTRIBUTE_LIST_OUT,
    DISTRIBUTE_LIST_OUT_INTERFACE,
    DISTRIBUTE_LIST_OUT_PREFIX,
    DISTRIBUTE_LIST_GATEWAY,
    MAXIMUM_PATHS_EBGP,
    MAXIMUM_PATHS_IBGP,
    NEIGHBOR,
    NETWORK,
    ROUTE_SERVER_CONTEXT,
    TABLE_MAP,
    TABLE_MAP_FILTER,
    TEMPLATE_PEER_POLICY,
    TEMPLATE_PEER_SESSION,
    COUNT
};

#define BGP_DEFAULTS(X) \
    X(Bgp, BGP_ADDITIONAL_PATHS_INSTALL, false) \
    X(Bgp, BGP_ADDITIONAL_PATHS_RECEIVE, false) \
    X(Bgp, BGP_ADDITIONAL_PATHS_SELECT_ALL, false) \
    X(Bgp, BGP_ADDITIONAL_PATHS_SELECT_BACKUP, false) \
    X(Bgp, BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL, false) \
    X(Bgp, BGP_ADDITIONAL_PATHS_SELECT_GROUP_BEST, false) \
    X(Bgp, BGP_ADVERTISE_BEST_EXTERNAL, false) \
    X(Bgp, BGP_AGGREGATE_TIMER, 30) \
    X(Bgp, BGP_ALWAYS_COMPARE_MED, false) \
    X(Bgp, BGP_AS_DOT_NOTATION, false) \
    X(Bgp, BGP_BEST_PATH_COMPARE_ROUTER_ID, false) \
    X(Bgp, BGP_BEST_PATH_COST_COMMUNITY_IGNORE, false) \
    X(Bgp, BGP_BEST_PATH_IGP_METRIC_IGNORE, false) \
    X(Bgp, BGP_BEST_PATH_MED_CONFED, false) \
    X(Bgp, BGP_BEST_PATH_MED_MISSING_AS_WORST, false) \
    X(Bgp, BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID, false) \
    X(Bgp, BGP_CLIENT_TO_CLIENT_REFLECTION, false) \
    X(Bgp, BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL, 60) \
    X(Bgp, BGP_DAMPENING, false) \
    X(Bgp, BGP_DAMPENING_HALF_LIFE, 15) \
    X(Bgp, BGP_DAMPENING_REUSE_THRESHOLD, 750) \
    X(Bgp, BGP_DAMPENING_SUPPRESS_THRESHOLD, 2000) \
    X(Bgp, BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME, 60) \
    X(Bgp, BGP_DETERMINISTIC_MED, false) \
    X(Bgp, BGP_DMZLINK_BW, false) \
    X(Bgp, BGP_ENFORCE_FIRST_AS, true) \
    X(Bgp, BGP_ENHANCED_ERROR, false) \
    X(Bgp, BGP_FAST_EXTERNAL_FAILOVER, true) \
    X(Bgp, BGP_GRACEFUL_RESTART, false) \
    X(Bgp, BGP_GRACEFUL_RESTART_EXTENDED, false) \
    X(Bgp, BGP_GRACEFUL_RESTART_RESTART_TIME, 120) \
    X(Bgp, BGP_GRACEFUL_RESTART_STALEPATH_TIME, 360) \
    X(Bgp, BGP_INJECT_MAP_COPY_ATTRIBUTES, false) \
    X(Bgp, BGP_LISTEN, false) \
    X(Bgp, BGP_LOG_NEIGHBOR_CHANGES, false) \
    X(Bgp, BGP_NEXT_HOP_TRACKING, true) \
    X(Bgp, BGP_RECURSIVE_HOST, true) \
    X(Bgp, BGP_REDISTRIBUTE_INTERNAL, false) \
    X(Bgp, BGP_REFRESH_MAX_EOR_TIME, 60) \
    X(Bgp, BGP_REFRESH_STALEPATH_TIME, 120) \
    X(Bgp, BGP_REGEX_DETERMINISTIC, true) \
    X(Bgp, BGP_ROUTE_MAP_PRIORITY, false) \
    X(Bgp, BGP_SCAN_TIME, 60) \
    X(Bgp, BGP_SLOW_PEER_DETECTION, false) \
    X(Bgp, BGP_SLOW_PEER_DETECTION_THRESHOLD, 60) \
    X(Bgp, BGP_SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC, false) \
    X(Bgp, BGP_SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC_PERMANENT, false) \
    X(Bgp, BGP_SOFT_RECONFIG_BACKUP, false) \
    X(Bgp, BGP_SUPPRESS_INACTIVE, false) \
    X(Bgp, DEFAULT_ORIGINATE, false) \
    X(Bgp, DISTANCE_BGP_EXTERNAL, 20) \
    X(Bgp, DISTANCE_BGP_INTERNAL, 200) \
    X(Bgp, DISTANCE_BGP_LOCAL, 200) \
    X(Bgp, DISTANCE_MBGP_EXTERNAL, 20) \
    X(Bgp, DISTANCE_MBGP_INTERNAL, 200) \
    X(Bgp, DISTANCE_MBGP_LOCAL, 200) \
    X(Bgp, DISTRIBUTE_LIST_IN_PREFIX, false) \
    X(Bgp, DISTRIBUTE_LIST_OUT_PREFIX, false) \
    X(Bgp, TABLE_MAP_FILTER, false)

CONFIG_DEFAULT_TABLE(BGP_DEFAULTS);

#define BGP_AGGREGATE_ADDRESS_FIELDS(X) \
    X(IPPrefix,    prefix) \
    X(std::string, advertiseMap) \
    X(bool,        asConfedSet) \
    X(std::string, attributeMap) \
    X(std::string, routeMap) \
    X(bool,        summaryOnly) \
    X(std::string, suppressMap)

DEFINE_TUPLE_SCHEMA(BgpAggregateAddress, BGP_AGGREGATE_ADDRESS_FIELDS)

using BgpRegistry = SubRegistry<__uint128_t, Bgp,
    ValueField<std::vector<BgpAggregateAddress::Tuple> CONFIG_INDEX_ARG(Bgp::AGGREGATE_ADDRESS)>,
    ReferenceContainer<BgpBaseRegistry CONFIG_INDEX_ARG(Bgp::BGP_BASE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ADDITIONAL_PATHS_INSTALL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ADDITIONAL_PATHS_RECEIVE)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::BGP_ADDITIONAL_PATHS_SELECT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ADDITIONAL_PATHS_SELECT_ALL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ADDITIONAL_PATHS_SELECT_BACKUP)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::BGP_ADDITIONAL_PATHS_SELECT_BEST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ADDITIONAL_PATHS_SELECT_GROUP_BEST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ADVERTISE_BEST_EXTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::BGP_AGGREGATE_TIMER)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ALWAYS_COMPARE_MED)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_AS_DOT_NOTATION)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_BEST_PATH_COMPARE_ROUTER_ID)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_BEST_PATH_COST_COMMUNITY_IGNORE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_BEST_PATH_IGP_METRIC_IGNORE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_BEST_PATH_MED_CONFED)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_BEST_PATH_MED_MISSING_AS_WORST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_CLIENT_TO_CLIENT_REFLECTION)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_CLUSTER_ID)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_CONFEDERATION_IDENTIFIER)>,
    ValueField<std::vector<uint32_t> CONFIG_INDEX_ARG(Bgp::BGP_CONFEDERATION_PEERS)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_DAMPENING)> ,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::BGP_DAMPENING_HALF_LIFE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_DAMPENING_REUSE_THRESHOLD)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_DAMPENING_SUPPRESS_THRESHOLD)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::BGP_DAMPENING_ROUTE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_DETERMINISTIC_MED)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_DMZLINK_BW)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ENFORCE_FIRST_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ENHANCED_ERROR)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_FAST_EXTERNAL_FAILOVER)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_GRACEFUL_RESTART)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_GRACEFUL_RESTART_EXTENDED)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_GRACEFUL_RESTART_RESTART_TIME)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_GRACEFUL_RESTART_STALEPATH_TIME)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::BGP_INJECT_MAP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::BGP_INJECT_MAP_EXIST_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_INJECT_MAP_COPY_ATTRIBUTES)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_LISTEN)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_LISTEN_LIMIT)>,
    ValueField<std::vector<std::tuple<uint32_t, uint32_t, std::string>> CONFIG_INDEX_ARG(Bgp::BGP_LISTEN_RANGE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_LOG_NEIGHBOR_CHANGES)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::BGP_MAX_AS_LIMIT)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_MAX_COMMUNITY_LIMIT)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_MAX_EXT_COMMUNITY_LIMIT)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::BGP_NEXT_HOP_ROUTE_MAP)>,
    OptionalValueField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NEXT_HOP_TRIGGER_DELAY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_NEXT_HOP_TRACKING)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_COLD_BOOT)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_NSF_SWITCHOVER)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_POST_BOOT)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_USER_INITIATED)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_RECURSIVE_HOST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_REDISTRIBUTE_INTERNAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_REFRESH_MAX_EOR_TIME)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_REFRESH_STALEPATH_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_REGEX_DETERMINISTIC)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ROUTE_MAP_PRIORITY)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_ROUTER_ID)>,
    ValueField<std::vector<std::tuple<
        IPAddress,
        uint16_t, // port
        uint16_t, // refresh time
        std::string, // ssh username
        std::string // ssh password
    >> CONFIG_INDEX_ARG(Bgp::BGP_RPKI_SERVER)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::BGP_SCAN_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_SLOW_PEER_DETECTION)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_SLOW_PEER_DETECTION_THRESHOLD)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC)>, // false = static
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_SLOW_PEER_SPLIT_UPDATE_GROUP_DYNAMIC_PERMANENT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_SOFT_RECONFIG_BACKUP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_SUPPRESS_INACTIVE)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_UPDATE_DELAY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::DEFAULT_ORIGINATE)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::DEFAULT_METRIC)>,
    ValueField<std::vector<std::tuple<uint8_t, std::vector<std::tuple<IPPrefix, std::string>>>> CONFIG_INDEX_ARG(Bgp::DISTANCE_RANGE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::DISTANCE_BGP_EXTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::DISTANCE_BGP_INTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::DISTANCE_BGP_LOCAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::DISTANCE_MBGP_EXTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::DISTANCE_MBGP_INTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::DISTANCE_MBGP_LOCAL)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::DISTRIBUTE_LIST_IN)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::DISTRIBUTE_LIST_IN_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::DISTRIBUTE_LIST_IN_PREFIX)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::DISTRIBUTE_LIST_OUT)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::DISTRIBUTE_LIST_OUT_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::DISTRIBUTE_LIST_OUT_PREFIX)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::DISTRIBUTE_LIST_GATEWAY)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::MAXIMUM_PATHS_EBGP)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::MAXIMUM_PATHS_IBGP)>,
    OwnedListField<BgpNeighborRegistry, IPAddress CONFIG_INDEX_ARG(Bgp::NEIGHBOR)>,
    ValueField<std::vector<std::tuple<IPPrefix, bool, std::string>> CONFIG_INDEX_ARG(Bgp::NETWORK)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::ROUTE_SERVER_CONTEXT)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::TABLE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::TABLE_MAP_FILTER)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::TEMPLATE_PEER_POLICY)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::TEMPLATE_PEER_SESSION)>
>;
}

#endif // OSPF_REGISTRY_H
