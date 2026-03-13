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
enum class BgpTransportBase
{
    KEEPALIVE_INTERVAL, // TODO
    HOLDTIME,
    MINIMUM_HOLDTIME,
    TRANSPORT_PATH_MTU_DISCOVERY, // TODO
    COUNT
};

#define BGP_TRANSPORT_BASE_DEFAULTS(X) \
    X(BgpTransportBase, KEEPALIVE_INTERVAL, 60) \
    X(BgpTransportBase, HOLDTIME, 180) \
    X(BgpTransportBase, TRANSPORT_PATH_MTU_DISCOVERY, false)

CONFIG_DEFAULT_TABLE(BGP_TRANSPORT_BASE_DEFAULTS);

using BgpBaseRegistry = SubRegistry<RegistryKey<20>, BgpTransportBase,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpTransportBase::KEEPALIVE_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpTransportBase::HOLDTIME)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(BgpTransportBase::MINIMUM_HOLDTIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpTransportBase::TRANSPORT_PATH_MTU_DISCOVERY)>
>;

enum class BgpAfBase
{
    ADDITIONAL_PATHS_RECEIVE,
    ADDITIONAL_PATHS_SEND,
    ADVERTISE_ADDITIONAL_PATHS_ALL,
    ADVERTISE_ADDITIONAL_PATHS_BEST,
    ADVERTISE_ADDITIONAL_GROUP_BEST,
    ADVERTISE_BEST_EXTERNAL,
    SLOW_PEER_MODE,
    SLOW_PEER_DETECTION,
    SLOW_PEER_DETECTION_THRESHOLD,
    COUNT
};

#define BGP_AF_BASE_DEFAULTS(X) \
    X(BgpAfBase, ADDITIONAL_PATHS_RECEIVE, false) \
    X(BgpAfBase, ADDITIONAL_PATHS_SEND, false) \
    X(BgpAfBase, ADVERTISE_ADDITIONAL_PATHS_ALL, false) \
    X(BgpAfBase, ADVERTISE_ADDITIONAL_GROUP_BEST, false) \
    X(BgpAfBase, ADVERTISE_BEST_EXTERNAL, false) \
    X(BgpAfBase, SLOW_PEER_DETECTION, false) \
    X(BgpAfBase, SLOW_PEER_DETECTION_THRESHOLD, 300)

CONFIG_DEFAULT_TABLE(BGP_AF_BASE_DEFAULTS);

using BgpAfBaseRegistry = SubRegistry<RegistryKey<20>, BgpAfBase,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::ADDITIONAL_PATHS_RECEIVE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::ADDITIONAL_PATHS_SEND)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_ALL)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_BEST)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::ADVERTISE_ADDITIONAL_GROUP_BEST)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::ADVERTISE_BEST_EXTERNAL)>,
    OptionalAtomicField<BGP::SlowPeerMode CONFIG_INDEX_ARG(BgpAfBase::SLOW_PEER_MODE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBaseRegistry::SLOW_PEER_DETECTION)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpAfBase::SLOW_PEER_DETECTION_THRESHOLD)>
>;

enum class BgpNeighbor
{
    AF_BASE,
    ACTIVATE,
    ADVERTISE_DIVERSE_PATH_BACKUP,
    ADVERTISE_DIVERSE_PATH_MPATH,
    ADVERTISE_MAP, // TODO
    ADVERTISE_MAP_EXIST_CONDITION, // TODO
    ADVERTISE_MAP_NON_EXIST_CONDITION, // TODO
    ADVERTISE_INTERVAL,
    ALLOWAS_IN,
    ALLOWAS_IN_OCCURANCES,
    ANNOUNCE_RPKI_STATE, // TODO
    ORF_BOTH,
    ORF_RECEIVE,
    ORF_SEND,
    DEFAULT_ORIGINATE,
    ORIGINATE_ROUTE_MAP, // TODO
    DISTRIBUTE_LIST_IN, // TODO
    DISTRIBUTE_LIST_IN_INTERFACE, // TODO
    DISTRIBUTE_LIST_OUT, // TODO
    DISTRIBUTE_LIST_OUT_INTERFACE, // TODO
    DMZLINK_BW, // TODO
    FILTER_LIST_IN, // TODO
    FILTER_LIST_OUT, // TODO
    INHERIT_PEER_POLICY,
    MAXIMUM_PREFIX,
    MAXIMUM_PREFIX_THRESHOLD,
    MAXIMUM_PREFIX_RESTART,
    MAXIMUM_PREFIX_WARNING_ONLY,
    NEXT_HOP_SELF,
    NEXT_HOP_SELF_ALL,
    NEXT_HOP_UNCHANGED,
    PREFIX_LIST_IN, // TODO
    PREFIX_LIST_OUT, // TODO:       prefix/distribute list can not co-exist
    REMOVE_PRIVATE_AS,
    REMOVE_PRIVATE_AS_ALL,
    ROUTE_MAP_IN, // TODO
    ROUTE_MAP_OUT, // TODO
    ROUTE_REFLECTOR_CLIENT,
    ROUTE_SERVER_CLIENT, // TODO
    ROUTE_SERVER_CLIENT_CONTEXT, // TODO
    SEND_COMMUNITY,
    SEND_COMMUNITY_BOTH,
    SEND_COMMUNITY_EXTENDED,
    SEND_COMMUNITY_STANDARD,
    SOFT_RECONFIGURATION,
    TRANSLATE_UPDATE, // TODO
    UNSUPPRESS_MAP, // TODO
    WEIGHT,
    COUNT
};

#define BGP_NEIGHBOR_DEFAULTS(X) \
    X(BgpNeighbor, ACTIVATE, false) \
    X(BgpNeighbor, ADVERTISE_DIVERSE_PATH_BACKUP, false) \
    X(BgpNeighbor, ADVERTISE_DIVERSE_PATH_MPATH, false) \
    X(BgpNeighbor, ADVERTISE_INTERVAL, 30) \
    X(BgpNeighbor, ALLOWAS_IN, false) \
    X(BgpNeighbor, ANNOUNCE_RPKI_STATE, false) \
    X(BgpNeighbor, ORF_BOTH, false) \
    X(BgpNeighbor, ORF_RECEIVE, false) \
    X(BgpNeighbor, ORF_SEND, false) \
    X(BgpNeighbor, DEFAULT_ORIGINATE, false) \
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
    X(BgpNeighbor, SOFT_RECONFIGURATION, false) \
    X(BgpNeighbor, TRANSLATE_UPDATE, false)

CONFIG_DEFAULT_TABLE(BGP_NEIGHBOR_DEFAULTS);

void BgpNeighborDefaultOriginate(void*);

using BgpNeighborRegistry = SubRegistry<RegistryKey<16>, BgpNeighbor,
    ReferenceContainer<BgpAfBaseRegistry CONFIG_INDEX_ARG(BgpNeighbor::AF_BASE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ACTIVATE)>,
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
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::DEFAULT_ORIGINATE)>,
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
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SOFT_RECONFIGURATION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::TRANSLATE_UPDATE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::UNSUPPRESS_MAP)>,
    OptionalValueField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::WEIGHT)>
>;

enum class BgpNeighborSession
{
    BGP_BASE,
    DESCRIPTION, // TODO
    DISABLE_CONNECTION_CHECK,
    EBGP_MULTIHOP, // TODO: need full tcp first
    EBGP_MAX_HOP_COUNT, // TODO: need full tcp first
    FALL_OVER, // TODO: needs rib callbacks
    FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE, // TODO
    FALL_OVER_BFD_MULTI_HOP, // TODO
    FALL_OVER_BFD_SINGLE_HOP, // TODO
    FALL_OVER_ROUTE_MAP, // TODO
    HAMODE_GRACEFUL_RESTART, // TODO
    INHERIT_PEER_SESSION,
    LOCAL_AS,
    LOCAL_AS_AS,
    LOCAL_AS_NO_PREPEND,
    LOCAL_AS_REPLACE_AS,
    LOCAL_AS_DUAL_AS,
    PASSWORD, // TODO
    PATH_ATTRIBUTE,
    PEER_GROUP,
    REMOTE_AS,
    SHUTDOWN,
    TRANSPORT_CONNECTION_MODE,
    TRANSPORT_MULTI_SESSION,
    TTL_SEC, // TODO
    TTL_SEC_HOP, // TODO
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

void BgpNeighborSessionShutdown(void*);
void BgpNeighborSessionPathAttribute(void*);

using BgpNeighborSessionRegistry = SubRegistry<RegistryKey<20>, BgpNeighborSession,
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
        uint8_t, // start
        uint8_t  // end
    >> CONFIG_INDEX_ARG(BgpNeighborSession::PATH_ATTRIBUTE), BgpNeighborSessionPathAttribute>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::PEER_GROUP)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighborSession::REMOTE_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::SHUTDOWN), BgpNeighborSessionShutdown>, // ad graceful
    OptionalAtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::TRANSPORT_CONNECTION_MODE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::TRANSPORT_MULTI_SESSION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::TTL_SEC)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighborSession::TTL_SEC_HOP)>,
    OwnedListField<BgpNeighborRegistry, uint32_t CONFIG_INDEX_ARG(BgpNeighborSession::AF_NEIGHBOR)>
>;

enum class BgpAddressFamily
{
    AF_BASE,
    AGGREGATE_ADDRESS,
    BGP_ADDITIONAL_PATHS_INSTALL, // TODO
    BGP_ADDITIONAL_PATHS_SELECT, // TODO
    BGP_ADDITIONAL_PATHS_SELECT_BACKUP,
    BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL,
    BGP_AGGREGATE_TIMER,
    BGP_BEST_PATH_COMPARE_ROUTER_ID,
    BGP_BEST_PATH_COST_COMMUNITY_IGNORE, // TODO
    BGP_BEST_PATH_IGP_METRIC_IGNORE,
    BGP_BEST_PATH_MED_CONFED, // TODO
    BGP_BEST_PATH_MED_MISSING_AS_WORST,
    BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID, // TODO
    BGP_DAMPENING, // TODO
    BGP_DAMPENING_HALF_LIFE, // TODO
    BGP_DAMPENING_REUSE_THRESHOLD, // TODO
    BGP_DAMPENING_SUPPRESS_THRESHOLD, // TODO
    BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME, // TODO
    BGP_DAMPENING_ROUTE_MAP, // TODO
    BGP_DMZLINK_BW, // both // TODO
    BGP_INJECT_MAP, // both // TODO
    BGP_INJECT_MAP_EXIST_MAP, // both // TODO
    BGP_INJECT_MAP_COPY_ATTRIBUTES, // both // TODO
    BGP_NEXT_HOP_ROUTE_MAP, // TODO
    BGP_NEXT_HOP_TRIGGER_DELAY, // TODO
    BGP_NEXT_HOP_TRACKING, // TODO
    BGP_RECURSIVE_HOST, // TODO
    BGP_REDISTRIBUTE_INTERNAL, // TODO
    BGP_ROUTE_MAP_PRIORITY, // TODO
    BGP_SOFT_RECONFIG_BACKUP, // TODO
    DEFAULT_ORIGINATE, // TODO
    DEFAULT_METRIC, // TODO
    DISTANCE_RANGE, // TODO
    DISTANCE_BGP_EXTERNAL, // TODO
    DISTANCE_BGP_INTERNAL, // TODO
    DISTANCE_BGP_LOCAL, // TODO
    DISTANCE_MBGP_EXTERNAL, // TODO
    DISTANCE_MBGP_INTERNAL, // TODO
    DISTANCE_MBGP_LOCAL, // TODO
    DISTRIBUTE_LIST_IN, // TODO
    DISTRIBUTE_LIST_IN_INTERFACE, // TODO
    DISTRIBUTE_LIST_IN_PREFIX, // TODO
    DISTRIBUTE_LIST_OUT, // TODO
    DISTRIBUTE_LIST_OUT_INTERFACE, // TODO
    DISTRIBUTE_LIST_OUT_PREFIX, // TODO
    DISTRIBUTE_LIST_GATEWAY, // TODO
    MAXIMUM_PATHS_EBGP,
    MAXIMUM_PATHS_IBGP,
    NETWORK, // TODO
    TABLE_MAP, // TODO
    TABLE_MAP_FILTER, // TODO
    COUNT
};

#define BGP_ADDRESS_FAMILY_DEFAULTS(X) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_INSTALL, false) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_SELECT_BACKUP, false) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL, false) \
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
    X(BgpAddressFamily, MAXIMUM_PATHS_EBGP, 1) \
    X(BgpAddressFamily, MAXIMUM_PATHS_IBGP, 1) \
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

using BgpAddressFamilyRegistry = SubRegistry<RegistryKey<8>, BgpAddressFamily,
    ReferenceContainer<BgpAfBaseRegistry CONFIG_INDEX_ARG(BgpAddressFamily::AF_BASE)>,
    ValueField<std::vector<BgpAggregateAddress::Tuple> CONFIG_INDEX_ARG(BgpAddressFamily::AGGREGATE_ADDRESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_INSTALL)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BACKUP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL)>,
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
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::MAXIMUM_PATHS_EBGP)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::MAXIMUM_PATHS_IBGP)>,
    ValueField<std::vector<std::tuple<IPPrefix, bool, std::string>> CONFIG_INDEX_ARG(BgpAddressFamily::NETWORK)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::TABLE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::TABLE_MAP_FILTER)>
>;

enum class Bgp
{
    BGP_BASE, // base // TODO
    ADDRESS_FAMILIES, // TODO
    BGP_ALWAYS_COMPARE_MED, // DONE // base // TODO
    BGP_AS_DOT_NOTATION, // base // TODO
    BGP_CLIENT_TO_CLIENT_REFLECTION, // base // TODO
    BGP_CLUSTER_ID, // base // TODO
    BGP_CONFEDERATION_IDENTIFIER, // base // TODO
    BGP_CONFEDERATION_PEERS, // base // TODO
    BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL, // base // TODO
    BGP_DETERMINISTIC_MED, // base // TODO
    BGP_DMZLINK_BW, // both // TODO
    BGP_ENFORCE_FIRST_AS, // base // TODO
    BGP_ENHANCED_ERROR, // base // TODO
    BGP_FAST_EXTERNAL_FAILOVER, // base // TODO
    BGP_GRACEFUL_RESTART, // base // TODO
    BGP_GRACEFUL_RESTART_EXTENDED, // base // TODO
    BGP_GRACEFUL_RESTART_RESTART_TIME, // base // TODO
    BGP_GRACEFUL_RESTART_STALEPATH_TIME, // base // TODO
    BGP_INJECT_MAP, // both // TODO
    BGP_INJECT_MAP_EXIST_MAP, // both // TODO
    BGP_INJECT_MAP_COPY_ATTRIBUTES, // both // TODO
    BGP_LISTEN, // base // TODO
    BGP_LISTEN_LIMIT, // base // TODO
    BGP_LISTEN_RANGE, // base // TODO
    BGP_LOG_NEIGHBOR_CHANGES, // base // TODO
    BGP_MAX_AS_LIMIT, // base // TODO
    BGP_MAX_COMMUNITY_LIMIT, // base // TODO
    BGP_MAX_EXT_COMMUNITY_LIMIT, // base // TODO
    BGP_NOPEERUP_DELAY_COLD_BOOT, // base // TODO
    BGP_NOPEERUP_DELAY_NSF_SWITCHOVER, // base // TODO
    BGP_NOPEERUP_DELAY_POST_BOOT, // base // TODO
    BGP_NOPEERUP_DELAY_USER_INITIATED, // base // TODO
    BGP_REFRESH_MAX_EOR_TIME, // base // TODO
    BGP_REFRESH_STALEPATH_TIME, // base // TODO
    BGP_REGEX_DETERMINISTIC, // base // TODO
    BGP_ROUTER_ID, // base // TODO
    BGP_RPKI_SERVER, // base // TODO
    BGP_SCAN_TIME, // base // TODO
    BGP_SUPPRESS_INACTIVE, // base // TODO
    BGP_UPDATE_DELAY, // base // TODO
    NEIGHBOR, // both // TODO
    ROUTE_SERVER_CONTEXT, // base // TODO
    TEMPLATE_PEER_POLICY, // base // TODO
    TEMPLATE_PEER_SESSION, // base // TODO
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

using BgpRegistry = SubRegistry<RegistryKey<4>, Bgp,
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
