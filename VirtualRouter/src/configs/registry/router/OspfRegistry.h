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

#include "configs/RegistryDefaultTable.hpp"

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

/**
 * @brief Configuration fields for an OSPF virtual link (placeholder — not yet implemented).
 * @ingroup OSPF
 */
enum class OspfVirtualLink
{
    COUNT
};

struct OspfVirtualLinkFields : FieldTuple<> {};

struct OspfVirtualLinkRegistry : public SubRegistry<OspfVirtualLinkRegistry, OspfVirtualLink, nullptr, OspfVirtualLinkFields> {};

/**
 * @brief Per-area OSPF configuration fields (type, authentication, stub cost, NSSA).
 * @ingroup OSPF
 */
enum class OspfArea
{
    AUTHENTICATION_TYPE,
    DEFAULT_COST,
    FILTER_LIST,
    AREA_TYPE,
    NSSA_DEFAULT_ORIGINATE,
    NSSA_DEFAULT_METRIC,
    NSSA_DEFAULT_METRIC_TYPE,
    NSSA_DEFAULT_ONLY,
    NSSA_NO_EXT,
    NSSA_NO_REDISTRIBUTION,
    NSSA_ALWAYS_TRANSLATE,
    NSSA_SUPPRESS_FA,
    RANGE,
    VIRTUAL_LINKS,
    COUNT
};

#define OSPF_AREA_DEFAULTS(X) \
    X(OspfArea, AUTHENTICATION_TYPE, ospf::AuthType::NULL_AUTH) \
    X(OspfArea, AREA_TYPE, ospf::AreaType::NORMAL) \
    X(OspfArea, NSSA_DEFAULT_ORIGINATE, false) \
    X(OspfArea, NSSA_DEFAULT_METRIC, 1) \
    X(OspfArea, NSSA_DEFAULT_METRIC_TYPE, true) \
    X(OspfArea, NSSA_DEFAULT_ONLY, false) \
    X(OspfArea, NSSA_NO_EXT, false) \
    X(OspfArea, NSSA_NO_REDISTRIBUTION, false) \
    X(OspfArea, NSSA_ALWAYS_TRANSLATE, false) \
    X(OspfArea, NSSA_SUPPRESS_FA, false)

CONFIG_DEFAULT_TABLE(OSPF_AREA_DEFAULTS);

void OspfAreaTypeChange(void* area);
void OspfAreaSycnRanges(void* area);

struct OspfAreaFields : FieldTuple<
    AtomicField<ospf::AuthType CONFIG_INDEX_ARG(OspfArea::AUTHENTICATION_TYPE)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(OspfArea::DEFAULT_COST)>,
    OptionalAtomicField<std::nullptr_t CONFIG_INDEX_ARG(OspfArea::FILTER_LIST)>, // TODO:
    AtomicField<ospf::AreaType CONFIG_INDEX_ARG(OspfArea::AREA_TYPE),
        OspfAreaTypeChange>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfArea::NSSA_DEFAULT_ORIGINATE)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(OspfArea::NSSA_DEFAULT_METRIC)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfArea::NSSA_DEFAULT_METRIC_TYPE)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfArea::NSSA_DEFAULT_ONLY)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfArea::NSSA_NO_EXT)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfArea::NSSA_NO_REDISTRIBUTION)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfArea::NSSA_ALWAYS_TRANSLATE)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfArea::NSSA_SUPPRESS_FA)>,
    ListField<std::tuple<types::IPPrefix, bool, std::optional<uint32_t>> CONFIG_INDEX_ARG(OspfArea::RANGE),
        OspfAreaSycnRanges>,
    ListField<std::vector<std::tuple<>> CONFIG_INDEX_ARG(OspfArea::VIRTUAL_LINKS)> // TODO:
> {};

/**
 * @brief Registry slot for one OSPF area configuration.
 * @ingroup OSPF
 */
struct OspfAreaRegistry : SubRegistry<OspfAreaRegistry, OspfArea, nullptr, OspfAreaFields> {};

/**
 * @brief OSPF process-level configuration fields (areas, timers, redistribution, SPF tuning).
 * @ingroup OSPF
 */
enum class Ospf
{
    AREA_CONFIGS,   
    REFERENCE_BANDWIDTH,
    BFD,
    LLS,
    OPAQUE,
    TRANSIT,
    DOMAIN_ID,
    SECONDARY_DOMAIN_ID,
    DEFAULT_ORIGINATE_ALWAYS,
    DEFAULT_ORIGINATE_METRIC,
    DEFAULT_ORIGINATE_METRIC_TYPE,
    DEFAULT_ORIGINATE_ROUTE_MAP,
    DEFAULT_METRIC,
    DISCARD_INTERNAL,
    DISCARD_INTERNAL_DISTANCE,
    DISCARD_EXTERNAL,
    DISCARD_EXTERNAL_DISTANCE,
    DISTANCE,EXTERNAL_DISTANCE,
    INTER_AREA_DISTANCE,
    INTRA_AREA_DISTANCE,
    DISTRIBUTE_LIST,
    DOMAIN_TAG,
    EVENT_LOG_ONE_SHOT,
    EVENT_LOG_PAUSE,
    EVENT_LOG_SIZE,
    IGNORE_MOSPF,
    SNMP_IFINDEX,
    ISPF,
    RETRANSMISSION_DC_LIMIT,
    RETRANSMISSION_NON_DC_LIMIT,
    LOG_ADJACENCY_CHANGES,
    LOG_ADJACENCY_DETAILS,
    LRC_FORWARDING_ADDRESS,
    LRC_INTER_AREA_SUMMARY,
    LRC_NSSA_TRANSLATION,
    MAX_METRIC_EXTERNAL,
    MAX_METRIC_EXTERNAL_OVERRIDE,
    MAX_METRIC_INCLUDE_STUB,
    MAX_METRIC_ON_STARTUP_TIME,
    MAX_METRIC_ON_STARTUP_WAIT_FOR_BGP,
    MAX_METRIC_SUMMARY_LSA,
    MAX_LSA,
    MAX_LSA_THRESHOLD,
    MAX_LSA_IGNORE_COUNT,
    MAX_LSA_IGNORE_TIME,
    MAX_LSA_RESET_TIME,
    MAX_LSA_WARNING_ONLY,
    MAXIMUM_PATHS,
    MPLS_LDP_AREAS,
    MPLS_TRAF_ENG_AREAS,
    MPLS_TRAF_ENG_INTERFACES,
    MPLS_TRAF_ENG_MESH_GROUP,
    MPLS_TRAF_ENG_MULTICAST_INACT,
    MPLS_TRAF_ENG_ROUTER_ID,
    NETWORKS,
    NEIGHBORS,
    NSF_CISCO_HELPER,
    NSF_STRICT_CHECKING,
    HELLO_QUEUE_DEPTH,
    PREFIX_PRIORITY_ROUTE_MAP,
    UPDATE_QUEUE_DEPTH,
    ROUTER_ID,
    SHUTDOWN,
    LSA_ARRIVAL,
    FLOOD_PACING,
    LSA_GROUP_PACING,
    RETRANSMISSION_PACING,
    TABLE_MAP,
    TABLE_MAP_FILTER,
    PRIORITY,
    REDISTRIBUTE,
    SNMP,
    SUMMARY_ADDRESS,
    LSA_THROTTLE_DELAY,
    LSA_THROTTLE_HOLD,
    LSA_THROTTLE_MAX,
    SPF_THROTTLE_DELAY,
    SPF_THROTTLE_HOLD,
    SPF_THROTTLE_MAX,
    TRAFFIC_SHARE_MIN,
    TTL_SEC,
    TTL_SEC_HOPS,
    COUNT,
};

#define OSPF_DEFAULTS(X) \
    X(Ospf, REFERENCE_BANDWIDTH, 100) \
    X(Ospf, BFD, false) \
    X(Ospf, LLS, true) \
    X(Ospf, OPAQUE, false) \
    X(Ospf, TRANSIT, false) \
    X(Ospf, DEFAULT_ORIGINATE_ALWAYS, false) \
    X(Ospf, DEFAULT_ORIGINATE_METRIC, 1) \
    X(Ospf, DEFAULT_ORIGINATE_METRIC_TYPE, true) \
    X(Ospf, DISCARD_INTERNAL, true) \
    X(Ospf, DISCARD_INTERNAL_DISTANCE, 110) \
    X(Ospf, DISCARD_EXTERNAL, true) \
    X(Ospf, DISCARD_EXTERNAL_DISTANCE, 110) \
    X(Ospf, EXTERNAL_DISTANCE, 110) \
    X(Ospf, INTER_AREA_DISTANCE, 110) \
    X(Ospf, INTRA_AREA_DISTANCE, 110) \
    X(Ospf, EVENT_LOG_ONE_SHOT, false) \
    X(Ospf, EVENT_LOG_PAUSE, false) \
    X(Ospf, EVENT_LOG_SIZE, 0) \
    X(Ospf, IGNORE_MOSPF, true) \
    X(Ospf, SNMP_IFINDEX, false) \
    X(Ospf, ISPF, false) \
    X(Ospf, LOG_ADJACENCY_CHANGES, false) \
    X(Ospf, LOG_ADJACENCY_DETAILS, false) \
    X(Ospf, LRC_FORWARDING_ADDRESS, true) \
    X(Ospf, LRC_INTER_AREA_SUMMARY, true) \
    X(Ospf, LRC_NSSA_TRANSLATION, false) \
    X(Ospf, MAX_METRIC_EXTERNAL, false) \
    X(Ospf, MAX_METRIC_EXTERNAL_OVERRIDE, 16711680) \
    X(Ospf, MAX_METRIC_INCLUDE_STUB, false) \
    X(Ospf, MAX_METRIC_ON_STARTUP_WAIT_FOR_BGP, false) \
    X(Ospf, MAX_METRIC_SUMMARY_LSA, false) \
    X(Ospf, MAX_LSA_THRESHOLD, 75) \
    X(Ospf, MAX_LSA_IGNORE_TIME, 5) \
    X(Ospf, MAX_LSA_WARNING_ONLY, false) \
    X(Ospf, MAXIMUM_PATHS, 4) \
    X(Ospf, MPLS_TRAF_ENG_MULTICAST_INACT, false) \
    X(Ospf, NSF_CISCO_HELPER, false) \
    X(Ospf, NSF_STRICT_CHECKING, false) \
    X(Ospf, SHUTDOWN, false) \
    X(Ospf, LSA_ARRIVAL, 1000) \
    X(Ospf, FLOOD_PACING, 33) \
    X(Ospf, LSA_GROUP_PACING, 240) \
    X(Ospf, RETRANSMISSION_PACING, 66) \
    X(Ospf, TABLE_MAP_FILTER, false) \
    X(Ospf, PRIORITY, 1) \
    X(Ospf, LSA_THROTTLE_DELAY, 0) \
    X(Ospf, LSA_THROTTLE_HOLD, 5000) \
    X(Ospf, LSA_THROTTLE_MAX, 5000) \
    X(Ospf, SPF_THROTTLE_DELAY, 5000) \
    X(Ospf, SPF_THROTTLE_HOLD, 10000) \
    X(Ospf, SPF_THROTTLE_MAX, 10000) \
    X(Ospf, TRAFFIC_SHARE_MIN, false) \
    X(Ospf, TTL_SEC, false) \
    X(Ospf, TTL_SEC_HOPS, 1)

CONFIG_DEFAULT_TABLE(OSPF_DEFAULTS);

void OspfSyncNeighbors(void* base);
void OspfSyncNetworks(void* base);
void OspfSyncSummaries(void* base);

struct OspfFields : FieldTuple<
    OwnedListField<OspfAreaRegistry, uint32_t CONFIG_INDEX_ARG(Ospf::AREA_CONFIGS)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::REFERENCE_BANDWIDTH)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::BFD)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::LLS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::OPAQUE)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::TRANSIT)>, // TODO: // virtual link
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::DOMAIN_ID)>, // TODO:
    ListField<std::vector<uint32_t> CONFIG_INDEX_ARG(Ospf::SECONDARY_DOMAIN_ID)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::DEFAULT_ORIGINATE_ALWAYS)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::DEFAULT_ORIGINATE_METRIC)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::DEFAULT_ORIGINATE_METRIC_TYPE)>,
    ListField<std::string CONFIG_INDEX_ARG(Ospf::DEFAULT_ORIGINATE_ROUTE_MAP)>, // TODO:
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::DEFAULT_METRIC)>, // XXX: REDISTRIBUTION
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::DISCARD_INTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::DISCARD_INTERNAL_DISTANCE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::DISCARD_EXTERNAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::DISCARD_EXTERNAL_DISTANCE)>,
    OptionalAtomicField<std::nullptr_t CONFIG_INDEX_ARG(Ospf::DISTANCE)>, // XXX:
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::EXTERNAL_DISTANCE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::INTER_AREA_DISTANCE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::INTRA_AREA_DISTANCE)>,
    OptionalAtomicField<std::nullptr_t CONFIG_INDEX_ARG(Ospf::DISTRIBUTE_LIST)>, // TODO:
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::DOMAIN_TAG)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::EVENT_LOG_ONE_SHOT)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::EVENT_LOG_PAUSE)>, // TODO:
    AtomicField<uint64_t CONFIG_INDEX_ARG(Ospf::EVENT_LOG_SIZE)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::IGNORE_MOSPF)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::SNMP_IFINDEX)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::ISPF)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::RETRANSMISSION_DC_LIMIT)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::RETRANSMISSION_NON_DC_LIMIT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::LOG_ADJACENCY_CHANGES)>, // XXX:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::LOG_ADJACENCY_DETAILS)>, // XXX:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::LRC_FORWARDING_ADDRESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::LRC_INTER_AREA_SUMMARY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::LRC_NSSA_TRANSLATION)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::MAX_METRIC_EXTERNAL)>, // XXX: REDISTRIBUTE
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::MAX_METRIC_EXTERNAL_OVERRIDE)>, // XXX: REDISTRIBUTE
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::MAX_METRIC_INCLUDE_STUB)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Ospf::MAX_METRIC_ON_STARTUP_TIME)>, // XXX: idk or care really
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::MAX_METRIC_ON_STARTUP_WAIT_FOR_BGP)>, // XXX: BGP
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::MAX_METRIC_SUMMARY_LSA)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::MAX_LSA)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::MAX_LSA_THRESHOLD)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Ospf::MAX_LSA_IGNORE_COUNT)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Ospf::MAX_LSA_IGNORE_TIME)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Ospf::MAX_LSA_RESET_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::MAX_LSA_WARNING_ONLY)>, // XXX:
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::MAXIMUM_PATHS)>,
    ListField<std::vector<uint32_t> CONFIG_INDEX_ARG(Ospf::MPLS_LDP_AREAS)>, // TODO:
    ListField<std::vector<uint32_t> CONFIG_INDEX_ARG(Ospf::MPLS_TRAF_ENG_AREAS)>, // TODO:
    ListField<std::vector<std::tuple<uint32_t, uint32_t>> CONFIG_INDEX_ARG(Ospf::MPLS_TRAF_ENG_INTERFACES)>, // TODO:
    ListField<std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> CONFIG_INDEX_ARG(Ospf::MPLS_TRAF_ENG_MESH_GROUP)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::MPLS_TRAF_ENG_MULTICAST_INACT)>, // TODO:
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::MPLS_TRAF_ENG_ROUTER_ID)>, // TODO:
    ListField<std::vector<std::tuple<types::IPPrefix, uint32_t>> CONFIG_INDEX_ARG(Ospf::NETWORKS),
        OspfSyncNetworks>,
    ListField<std::vector<std::tuple<
        types::IPAddress,
        std::optional<uint16_t>,
        std::optional<bool>,
        std::optional<uint16_t>,
        std::optional<uint8_t>
    >> CONFIG_INDEX_ARG(Ospf::NEIGHBORS),
        OspfSyncNeighbors>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::NSF_CISCO_HELPER)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::NSF_STRICT_CHECKING)>, // TODO:
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::HELLO_QUEUE_DEPTH)>, // XXX:
    ListField<std::string CONFIG_INDEX_ARG(Ospf::PREFIX_PRIORITY_ROUTE_MAP)>, // TODO:
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::UPDATE_QUEUE_DEPTH)>, // XXX:
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::ROUTER_ID)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::SHUTDOWN)>, // XXX:
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::LSA_ARRIVAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::FLOOD_PACING)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Ospf::LSA_GROUP_PACING)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::RETRANSMISSION_PACING)>,
    ListField<std::string CONFIG_INDEX_ARG(Ospf::TABLE_MAP)>, // TODO:
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::TABLE_MAP_FILTER)>, // TODO:
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::PRIORITY)>,
    OptionalAtomicField<std::nullptr_t CONFIG_INDEX_ARG(Ospf::REDISTRIBUTE)>, // TODO:
    OptionalAtomicField<std::nullptr_t CONFIG_INDEX_ARG(Ospf::SNMP)>, // TODO:
    ListField<std::vector<std::tuple<types::IPPrefix, bool, bool, std::optional<uint32_t>>> CONFIG_INDEX_ARG(Ospf::SUMMARY_ADDRESS),
        OspfSyncSummaries>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::LSA_THROTTLE_DELAY)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::LSA_THROTTLE_HOLD)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::LSA_THROTTLE_MAX)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::SPF_THROTTLE_DELAY)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::SPF_THROTTLE_HOLD)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Ospf::SPF_THROTTLE_MAX)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::TRAFFIC_SHARE_MIN)>,
    AtomicField<bool CONFIG_INDEX_ARG(Ospf::TTL_SEC)>, // XXX:
    AtomicField<uint8_t CONFIG_INDEX_ARG(Ospf::TTL_SEC_HOPS)> // XXX:
> {};

/**
 * @brief Registry slot for one OSPF process instance.
 * @ingroup OSPF
 */
struct OspfRegistry : SubRegistry<OspfRegistry, Ospf, nullptr, OspfFields> {};

/**
 * @brief OSPFv3 address-family process container fields.
 * @ingroup OSPF
 */
enum class Ospfv3AddressFamily
{
    IPV4,
    IPV6,
    COUNT
};

struct Ospfv3AddressFamilyFields : FieldTuple<
    RegistryContainer<OspfRegistry CONFIG_INDEX_ARG(Ospfv3AddressFamily::IPV4)>,
    RegistryContainer<OspfRegistry CONFIG_INDEX_ARG(Ospfv3AddressFamily::IPV6)>
> {};

/**
 * @brief Registry slot for the OSPFv3 address-family process container.
 * @ingroup OSPF
 */
struct Ospfv3AddressFamilyRegistry : SubRegistry<Ospfv3AddressFamilyRegistry, Ospfv3AddressFamily, nullptr, Ospfv3AddressFamilyFields> {};
}

#endif // OSPF_REGISTRY_H
