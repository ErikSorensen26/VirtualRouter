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

namespace BGP
{
enum class SlowPeerMode
{
    STATIC,
    DYNAMIC,
    DYNAMIC_PERMANENT
};
}

namespace Config
{
inline __uint128_t generateBgpKey(uint32_t vrf, uint32_t as, AddressFamily af)
{
    uint8_t addressFamily = af == AddressFamily::NONE ? 0
        : af == AddressFamily::IPv4 ? 1 : 2;

    __uint128_t key = 0;
    key |= __uint128_t(addressFamily) & maskU128Bits(2);
    key |= __uint128_t(vrf) << 2;
    key |= __uint128_t(as) << 34;
    return key;
}

enum class BgpTransportBase
{
    KEEPALIVE_INTERVAL,
    HOLDTIME, // XXX
    MINIMUM_HOLDTIME,
    TRANSPORT_PATH_MTU_DISCOVERY,
    COUNT
};

#define BGP_TRANSPORT_BASE_DEFAULTS(X) \
    X(BgpTransportBase, KEEPALIVE_INTERVAL, 60) \
    X(BgpTransportBase, HOLDTIME, 180) \
    X(BgpTransportBase, TRANSPORT_PATH_MTU_DISCOVERY, false)

CONFIG_DEFAULT_TABLE(BGP_TRANSPORT_BASE_DEFAULTS);

using BgpBaseRegistry = SubRegistry<__uint128_t, BgpTransportBase,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpTransportBase::KEEPALIVE_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpTransportBase::HOLDTIME)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(BgpTransportBase::MINIMUM_HOLDTIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpTransportBase::TRANSPORT_PATH_MTU_DISCOVERY)>
>;

enum class BgpNeighbor
{
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
    DISTRIBUTE_LIST_IN,
    DISTRIBUTE_LIST_IN_INTERFACE,
    DISTRIBUTE_LIST_OUT,
    DISTRIBUTE_LIST_OUT_INTERFACE,
    DMZLINK_BW,
    FILTER_LIST_IN,
    FILTER_LIST_OUT,
    INHERIT_PEER_POLICY,
    MAXIMUM_PREFIX,
    MAXIMUM_PREFIX_THRESHOLD,
    MAXIMUM_PREFIX_RESTART,
    MAXIMUM_PREFIX_WARNING_ONLY,
    NEXT_HOP_SELF,
    NEXT_HOP_SELF_ALL,
    NEXT_HOP_UNCHANGED,
    PREFIX_LIST_IN,
    PREFIX_LIST_OUT,
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
    SLOW_PEER_MODE,
    SLOW_PEER_DETECTION,
    SLOW_PEER_DETECTION_THRESHOLD,
    SOFT_RECONFIGURATION,
    TRANSLATE_UPDATE,
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
    X(BgpNeighbor, DMZLINK_BW, false) \
    X(BgpNeighbor, MAXIMUM_PREFIX_WARNING_ONLY, false) \
    X(BgpNeighbor, NEXT_HOP_SELF, false) \
    X(BgpNeighbor, NEXT_HOP_SELF_ALL, false) \
    X(BgpNeighbor, NEXT_HOP_UNCHANGED, false) \
    X(BgpNeighbor, REMOVE_PRIVATE_AS, false) \
    X(BgpNeighbor, REMOVE_PRIVATE_AS_ALL, false) \
    X(BgpNeighbor, ROUTE_REFLECTOR_CLIENT, false) \
    X(BgpNeighbor, ROUTE_SERVER_CLIENT, false) \
    X(BgpNeighbor, SEND_COMMUNITY, false) \
    X(BgpNeighbor, SEND_COMMUNITY_BOTH, false) \
    X(BgpNeighbor, SEND_COMMUNITY_EXTENDED, false) \
    X(BgpNeighbor, SEND_COMMUNITY_STANDARD, false) \
    X(BgpNeighbor, SLOW_PEER_DETECTION, false) \
    X(BgpNeighbor, SLOW_PEER_DETECTION_THRESHOLD, 60) \
    X(BgpNeighbor, SOFT_RECONFIGURATION, false) \
    X(BgpNeighbor, TRANSLATE_UPDATE, false) \
    X(BgpNeighbor, WEIGHT, 0)

CONFIG_DEFAULT_TABLE(BGP_NEIGHBOR_DEFAULTS);

using BgpNeighborRegistry = SubRegistry<__uint128_t, BgpNeighbor,
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
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_IN)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_IN_INTERFACE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_OUT)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_OUT_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::DMZLINK_BW)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::FILTER_LIST_IN)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::FILTER_LIST_OUT)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::INHERIT_PEER_POLICY)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX_THRESHOLD)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX_RESTART)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX_WARNING_ONLY)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::NEXT_HOP_SELF)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::NEXT_HOP_SELF_ALL)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::NEXT_HOP_UNCHANGED)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::PREFIX_LIST_IN)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::PREFIX_LIST_OUT)>,
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
    OptionalAtomicField<BGP::SlowPeerMode CONFIG_INDEX_ARG(BgpNeighbor::SLOW_PEER_MODE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SLOW_PEER_DETECTION)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::SLOW_PEER_DETECTION_THRESHOLD)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SOFT_RECONFIGURATION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::TRANSLATE_UPDATE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::UNSUPPRESS_MAP)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::WEIGHT)>
>;

enum class BgpNeighborSession
{
    BGP_BASE,
    DESCRIPTION,
    DISABLE_CONNECTION_CHECK,
    EBGP_MULTIHOP,
    EBGP_MAX_HOP_COUNT,
    FALL_OVER,
    FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE,
    FALL_OVER_BFD_MULTI_HOP,
    FALL_OVER_BFD_SINGLE_HOP,
    FALL_OVER_ROUTE_MAP,
    HAMODE_GRACEFUL_RESTART,
    INHERIT_PEER_SESSION,
    LOCAL_AS,
    LOCAL_AS_AS,
    LOCAL_AS_NO_PREPEND,
    LOCAL_AS_REPLACE_AS,
    LOCAL_AS_DUAL_AS,
    PASSWORD,
    PATH_ATTRIBUTE,
    PEER_GROUP,
    REMOTE_AS,
    SHUTDOWN,
    TRANSPORT_CONNECTION_MODE,
    TRANSPORT_MULTI_SESSION,
    TTL_SEC,
    TTL_SEC_HOP,
    AF_NEIGHBOR,
    COUNT
};

#define BGP_NEIGHBOR_SESSION_DEFAULTS(X) \
    X(BgpNeighborSession, DISABLE_CONNECTION_CHECK, false) \
    X(BgpNeighborSession, EBGP_MULTIHOP, false) \
    X(BgpNeighborSession, EBGP_MAX_HOP_COUNT, 1) \
    X(BgpNeighborSession, FALL_OVER, false) \
    X(BgpNeighborSession, FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE, false) \
    X(BgpNeighborSession, FALL_OVER_BFD_MULTI_HOP, false) \
    X(BgpNeighborSession, FALL_OVER_BFD_SINGLE_HOP, false) \
    X(BgpNeighborSession, LOCAL_AS, false) \
    X(BgpNeighborSession, LOCAL_AS_NO_PREPEND, false) \
    X(BgpNeighborSession, LOCAL_AS_REPLACE_AS, false) \
    X(BgpNeighborSession, LOCAL_AS_DUAL_AS, false) \
    X(BgpNeighborSession, SHUTDOWN, false) \
    X(BgpNeighborSession, TRANSPORT_MULTI_SESSION, false) \
    X(BgpNeighborSession, TTL_SEC, false) \
    X(BgpNeighborSession, TTL_SEC_HOP, 1)

CONFIG_DEFAULT_TABLE(BGP_NEIGHBOR_SESSION_DEFAULTS);

using BgpNeighborSessionRegistry = SubRegistry<__uint128_t, BgpNeighborSession,
    ReferenceContainer<BgpBaseRegistry CONFIG_INDEX_ARG(BgpNeighborSession::BGP_BASE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::DESCRIPTION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::DISABLE_CONNECTION_CHECK)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::EBGP_MULTIHOP)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighborSession::EBGP_MAX_HOP_COUNT)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::FALL_OVER)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::FALL_OVER_BFD_MULTI_HOP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::FALL_OVER_BFD_SINGLE_HOP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::FALL_OVER_ROUTE_MAP)>,
    OptionalAtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::HAMODE_GRACEFUL_RESTART)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::INHERIT_PEER_SESSION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::LOCAL_AS)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighborSession::LOCAL_AS_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::LOCAL_AS_NO_PREPEND)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::LOCAL_AS_REPLACE_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::LOCAL_AS_DUAL_AS)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::PASSWORD)>,
    ValueField<std::vector<std::tuple<
        bool,    // true = discard, false = treat-as-withdraw
        bool,    // isRanged
        uint8_t, // start
        uint8_t  // end
    >> CONFIG_INDEX_ARG(BgpNeighborSession::PATH_ATTRIBUTE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::PEER_GROUP)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighborSession::REMOTE_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::SHUTDOWN)>, // ad graceful
    OptionalAtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::TRANSPORT_CONNECTION_MODE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::TRANSPORT_MULTI_SESSION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::TTL_SEC)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighborSession::TTL_SEC_HOP)>,
    OwnedListField<BgpNeighborRegistry, IPAddress CONFIG_INDEX_ARG(BgpNeighborSession::AF_NEIGHBOR)>
>;

enum class BgpAddressFamily
{
    AGGREGATE_ADDRESS,
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
    BGP_BEST_PATH_COMPARE_ROUTER_ID,
    BGP_BEST_PATH_COST_COMMUNITY_IGNORE,
    BGP_BEST_PATH_IGP_METRIC_IGNORE,
    BGP_BEST_PATH_MED_CONFED,
    BGP_BEST_PATH_MED_MISSING_AS_WORST,
    BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID,
    BGP_DAMPENING,
    BGP_DAMPENING_HALF_LIFE,
    BGP_DAMPENING_REUSE_THRESHOLD,
    BGP_DAMPENING_SUPPRESS_THRESHOLD,
    BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME,
    BGP_DAMPENING_ROUTE_MAP,
    BGP_DMZLINK_BW, // both
    BGP_INJECT_MAP, // both
    BGP_INJECT_MAP_EXIST_MAP, // both
    BGP_INJECT_MAP_COPY_ATTRIBUTES, // both
    BGP_NEXT_HOP_ROUTE_MAP,
    BGP_NEXT_HOP_TRIGGER_DELAY,
    BGP_NEXT_HOP_TRACKING,
    BGP_RECURSIVE_HOST,
    BGP_REDISTRIBUTE_INTERNAL,
    BGP_ROUTE_MAP_PRIORITY,
    SLOW_PEER_DETECTION,
    SLOW_PEER_DETECTION_THRESHOLD,
    SLOW_PEER_MODE,
    BGP_SOFT_RECONFIG_BACKUP,
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
    NETWORK,
    TABLE_MAP,
    TABLE_MAP_FILTER,
    COUNT
};

#define BGP_ADDRESS_FAMILY_DEFAULTS(X) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_INSTALL, false) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_RECEIVE, false) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_SELECT_ALL, false) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_SELECT_BACKUP, false) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL, false) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_SELECT_GROUP_BEST, false) \
    X(BgpAddressFamily, BGP_ADVERTISE_BEST_EXTERNAL, false) \
    X(BgpAddressFamily, BGP_AGGREGATE_TIMER, 30) \
    X(BgpAddressFamily, BGP_BEST_PATH_COMPARE_ROUTER_ID, false) \
    X(BgpAddressFamily, BGP_BEST_PATH_COST_COMMUNITY_IGNORE, false) \
    X(BgpAddressFamily, BGP_BEST_PATH_IGP_METRIC_IGNORE, false) \
    X(BgpAddressFamily, BGP_BEST_PATH_MED_CONFED, false) \
    X(BgpAddressFamily, BGP_BEST_PATH_MED_MISSING_AS_WORST, false) \
    X(BgpAddressFamily, BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID, false) \
    X(BgpAddressFamily, BGP_DAMPENING, false) \
    X(BgpAddressFamily, BGP_DAMPENING_HALF_LIFE, 15) \
    X(BgpAddressFamily, BGP_DAMPENING_REUSE_THRESHOLD, 750) \
    X(BgpAddressFamily, BGP_DAMPENING_SUPPRESS_THRESHOLD, 2000) \
    X(BgpAddressFamily, BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME, 60) \
    X(BgpAddressFamily, BGP_DMZLINK_BW, false) \
    X(BgpAddressFamily, BGP_INJECT_MAP_COPY_ATTRIBUTES, false) \
    X(BgpAddressFamily, BGP_NEXT_HOP_TRACKING, true) \
    X(BgpAddressFamily, BGP_RECURSIVE_HOST, true) \
    X(BgpAddressFamily, BGP_REDISTRIBUTE_INTERNAL, false) \
    X(BgpAddressFamily, BGP_ROUTE_MAP_PRIORITY, false) \
    X(BgpAddressFamily, SLOW_PEER_DETECTION, false) \
    X(BgpAddressFamily, SLOW_PEER_DETECTION_THRESHOLD, 60) \
    X(BgpAddressFamily, BGP_SOFT_RECONFIG_BACKUP, false) \
    X(BgpAddressFamily, DEFAULT_ORIGINATE, false) \
    X(BgpAddressFamily, DISTANCE_BGP_EXTERNAL, 20) \
    X(BgpAddressFamily, DISTANCE_BGP_INTERNAL, 200) \
    X(BgpAddressFamily, DISTANCE_BGP_LOCAL, 200) \
    X(BgpAddressFamily, DISTANCE_MBGP_EXTERNAL, 20) \
    X(BgpAddressFamily, DISTANCE_MBGP_INTERNAL, 200) \
    X(BgpAddressFamily, DISTANCE_MBGP_LOCAL, 200) \
    X(BgpAddressFamily, DISTRIBUTE_LIST_IN_PREFIX, false) \
    X(BgpAddressFamily, DISTRIBUTE_LIST_OUT_PREFIX, false) \
    X(BgpAddressFamily, TABLE_MAP_FILTER, false)

CONFIG_DEFAULT_TABLE(BGP_ADDRESS_FAMILY_DEFAULTS);

#define BGP_AGGREGATE_ADDRESS_FIELDS(X) \
    X(IPPrefix,    prefix) \
    X(std::string, advertiseMap) \
    X(bool,        asConfedSet) \
    X(std::string, attributeMap) \
    X(std::string, routeMap) \
    X(bool,        summaryOnly) \
    X(std::string, suppressMap)

DEFINE_TUPLE_SCHEMA(BgpAggregateAddress, BGP_AGGREGATE_ADDRESS_FIELDS)

using BgpAddressFamilyRegistry = SubRegistry<__uint128_t, BgpAddressFamily,
    ValueField<std::vector<BgpAggregateAddress::Tuple> CONFIG_INDEX_ARG(BgpAddressFamily::AGGREGATE_ADDRESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_INSTALL)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_RECEIVE)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_ALL)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BACKUP)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BEST)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_GROUP_BEST)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADVERTISE_BEST_EXTERNAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_AGGREGATE_TIMER)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_COMPARE_ROUTER_ID)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_COST_COMMUNITY_IGNORE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_IGP_METRIC_IGNORE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_MED_CONFED)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_MED_MISSING_AS_WORST)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING)> ,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING_HALF_LIFE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING_REUSE_THRESHOLD)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING_SUPPRESS_THRESHOLD)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING_ROUTE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DMZLINK_BW)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::BGP_INJECT_MAP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::BGP_INJECT_MAP_EXIST_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_INJECT_MAP_COPY_ATTRIBUTES)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::BGP_NEXT_HOP_ROUTE_MAP)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_NEXT_HOP_TRIGGER_DELAY)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_NEXT_HOP_TRACKING)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_RECURSIVE_HOST)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_REDISTRIBUTE_INTERNAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ROUTE_MAP_PRIORITY)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::SLOW_PEER_DETECTION)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpAddressFamily::SLOW_PEER_DETECTION_THRESHOLD)>,
    OptionalAtomicField<BGP::SlowPeerMode CONFIG_INDEX_ARG(BgpAddressFamily::SLOW_PEER_MODE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_SOFT_RECONFIG_BACKUP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::DEFAULT_ORIGINATE)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpAddressFamily::DEFAULT_METRIC)>,
    ValueField<std::vector<std::tuple<uint8_t, std::vector<std::tuple<IPPrefix, std::string>>>> CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_RANGE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_BGP_EXTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_BGP_INTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_BGP_LOCAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_MBGP_EXTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_MBGP_INTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_MBGP_LOCAL)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_IN)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_IN_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_IN_PREFIX)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_OUT)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_OUT_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_OUT_PREFIX)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_GATEWAY)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::MAXIMUM_PATHS_EBGP)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::MAXIMUM_PATHS_IBGP)>,
    ValueField<std::vector<std::tuple<IPPrefix, bool, std::string>> CONFIG_INDEX_ARG(BgpAddressFamily::NETWORK)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::TABLE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::TABLE_MAP_FILTER)>
>;

enum class Bgp
{
    BGP_BASE, // base
    ADDRESS_FAMILIES,
    BGP_ALWAYS_COMPARE_MED, // DONE // base
    BGP_AS_DOT_NOTATION, // base
    BGP_CLIENT_TO_CLIENT_REFLECTION, // base
    BGP_CLUSTER_ID, // base
    BGP_CONFEDERATION_IDENTIFIER, // base
    BGP_CONFEDERATION_PEERS, // base
    BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL, // base
    BGP_DETERMINISTIC_MED, // base
    BGP_DMZLINK_BW, // both
    BGP_ENFORCE_FIRST_AS, // base
    BGP_ENHANCED_ERROR, // base
    BGP_FAST_EXTERNAL_FAILOVER, // base
    BGP_GRACEFUL_RESTART, // base
    BGP_GRACEFUL_RESTART_EXTENDED, // base
    BGP_GRACEFUL_RESTART_RESTART_TIME, // base
    BGP_GRACEFUL_RESTART_STALEPATH_TIME, // base
    BGP_INJECT_MAP, // both
    BGP_INJECT_MAP_EXIST_MAP, // both
    BGP_INJECT_MAP_COPY_ATTRIBUTES, // both
    BGP_LISTEN, // base
    BGP_LISTEN_LIMIT, // base
    BGP_LISTEN_RANGE, // base
    BGP_LOG_NEIGHBOR_CHANGES, // base
    BGP_MAX_AS_LIMIT, // base
    BGP_MAX_COMMUNITY_LIMIT, // base
    BGP_MAX_EXT_COMMUNITY_LIMIT, // base
    BGP_NOPEERUP_DELAY_COLD_BOOT, // base
    BGP_NOPEERUP_DELAY_NSF_SWITCHOVER, // base
    BGP_NOPEERUP_DELAY_POST_BOOT, // base
    BGP_NOPEERUP_DELAY_USER_INITIATED, // base
    BGP_REFRESH_MAX_EOR_TIME, // base
    BGP_REFRESH_STALEPATH_TIME, // base
    BGP_REGEX_DETERMINISTIC, // base
    BGP_ROUTER_ID, // base
    BGP_RPKI_SERVER, // base
    BGP_SCAN_TIME, // base
    BGP_SUPPRESS_INACTIVE, // base
    BGP_UPDATE_DELAY, // base
    NEIGHBOR, // both
    ROUTE_SERVER_CONTEXT, // base
    TEMPLATE_PEER_POLICY, // base
    TEMPLATE_PEER_SESSION, // base
    COUNT
};

#define BGP_DEFAULTS(X) \
    X(Bgp, BGP_ALWAYS_COMPARE_MED, false) \
    X(Bgp, BGP_AS_DOT_NOTATION, false) \
    X(Bgp, BGP_CLIENT_TO_CLIENT_REFLECTION, false) \
    X(Bgp, BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL, 60) \
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
    X(Bgp, BGP_REFRESH_MAX_EOR_TIME, 60) \
    X(Bgp, BGP_REFRESH_STALEPATH_TIME, 120) \
    X(Bgp, BGP_REGEX_DETERMINISTIC, true) \
    X(Bgp, BGP_SCAN_TIME, 60) \
    X(Bgp, BGP_SUPPRESS_INACTIVE, false)

CONFIG_DEFAULT_TABLE(BGP_DEFAULTS);

using BgpRegistry = SubRegistry<__uint128_t, Bgp,
    ReferenceContainer<BgpBaseRegistry CONFIG_INDEX_ARG(Bgp::BGP_BASE)>,
    OwnedListField<BgpAddressFamilyRegistry, uint32_t CONFIG_INDEX_ARG(Bgp::ADDRESS_FAMILIES)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ALWAYS_COMPARE_MED)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_AS_DOT_NOTATION)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_CLIENT_TO_CLIENT_REFLECTION)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_CLUSTER_ID)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_CONFEDERATION_IDENTIFIER)>,
    ValueField<std::vector<uint32_t> CONFIG_INDEX_ARG(Bgp::BGP_CONFEDERATION_PEERS)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL)>,
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
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_COLD_BOOT)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_NSF_SWITCHOVER)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_POST_BOOT)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_USER_INITIATED)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_REFRESH_MAX_EOR_TIME)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_REFRESH_STALEPATH_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_REGEX_DETERMINISTIC)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_ROUTER_ID)>,
    ValueField<std::vector<std::tuple<
        IPAddress,
        uint16_t, // port
        uint16_t, // refresh time
        std::string, // ssh username
        std::string // ssh password
    >> CONFIG_INDEX_ARG(Bgp::BGP_RPKI_SERVER)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::BGP_SCAN_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_SUPPRESS_INACTIVE)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_UPDATE_DELAY)>,
    OwnedListField<BgpNeighborSessionRegistry, IPAddress CONFIG_INDEX_ARG(Bgp::NEIGHBOR)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::ROUTE_SERVER_CONTEXT)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::TEMPLATE_PEER_POLICY)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Bgp::TEMPLATE_PEER_SESSION)>
>;
}

#endif // BGP_REGISTRY_H
