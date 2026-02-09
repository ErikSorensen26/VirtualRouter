// OspfRegistry.h

#ifndef OSPF_REGISTRY_H
#define OSPF_REGISTRY_H

#include "OspfInterfaceRegistry.h"
#include <RegistryTemplate.hpp>
#include <tuple>
#include <IPAddress.hpp>
#include <string>

namespace OSPF
{
class OspfProcess;
class OspfArea;

enum class AreaType
{
    NORMAL,
    STUB,
    TOTALLY_STUB, // stub no-summary
    NSSA, // NSSA
    TOTALLY_NSSA // NSSA_NO_SUMMARY
};
}

namespace Config
{
inline __uint128_t generateOspfVirtualLinkKey(__uint128_t areaKey, uint32_t ip)
{
    __uint128_t key = 0;
    key |= __uint128_t(ip);
    key |= (areaKey & maskU128Bits(96)) << 32;
    return key;
}

enum class OspfVirtualLink
{
    COUNT
};

using OspfVirtualLinkRegistry = SubRegistry<__uint128_t, OspfVirtualLink, OSPF::OspfArea>;

inline __uint128_t generateOspfAreaKey(__uint128_t topoKey, uint32_t areaId)
{
    __uint128_t key = 0;
    key |= __uint128_t(areaId);
    key |= (topoKey & maskU128Bits(99)) << 32;
    return key;
}

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

void OspfAreaTypeChange(OSPF::OspfArea& area);
void OspfAreaSycnRanges(OSPF::OspfArea& area);

using OspfAreaRegistry = SubRegistry<__uint128_t, OspfArea, OSPF::OspfArea,
    AtomicField<OSPF::AuthType, OSPF::AuthType::NULL_AUTH, OspfArea::AUTHENTICATION_TYPE>,
    OptionalAtomicField<uint32_t, OspfArea::DEFAULT_COST>,
    OptionalAtomicField<std::nullptr_t, OspfArea::FILTER_LIST>, // TODO:
    AtomicField<OSPF::AreaType, OSPF::AreaType::NORMAL, OspfArea::AREA_TYPE,
        OSPF::OspfArea, OspfAreaTypeChange>,
    AtomicField<bool, false, OspfArea::NSSA_DEFAULT_ORIGINATE>,
    AtomicField<uint32_t, 1, OspfArea::NSSA_DEFAULT_METRIC>,
    AtomicField<bool, true, OspfArea::NSSA_DEFAULT_METRIC_TYPE>,
    AtomicField<bool, false, OspfArea::NSSA_DEFAULT_ONLY>,
    AtomicField<bool, false, OspfArea::NSSA_NO_EXT>,
    AtomicField<bool, false, OspfArea::NSSA_NO_REDISTRIBUTION>,
    AtomicField<bool, false, OspfArea::NSSA_ALWAYS_TRANSLATE>,
    AtomicField<bool, false, OspfArea::NSSA_SUPPRESS_FA>,
    ValueField<std::vector<std::tuple<IPPrefix, bool, std::optional<uint32_t>>>, OspfArea::RANGE,
        OSPF::OspfArea, OspfAreaSycnRanges>,
    ValueField<std::vector<std::tuple<>>, OspfArea::VIRTUAL_LINKS> // TODO:
>;

inline __uint128_t generateOspfKey(uint32_t vrf, uint32_t procId, AddressFamily af /*uint8_t*/, bool isV3)
{
    uint8_t addressFamily = af == AddressFamily::NONE ? 0
        : af == AddressFamily::IPv4 ? 1 : 2;

    __uint128_t key = 0;
    key |= __uint128_t(addressFamily) & maskU128Bits(2);
    key |= __uint128_t(isV3 ? 1 : 0) << 2;
    key |= __uint128_t(vrf) << 3;
    key |= __uint128_t(procId) << 35;
    return key;
}

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


void OspfSyncNeighbors(OSPF::OspfProcess& base);
void OspfSyncNetworks(OSPF::OspfProcess& base);
void OspfSyncSummaries(OSPF::OspfProcess& base);

using OspfRegistry = SubRegistry<__uint128_t, Ospf, OSPF::OspfProcess,
    OwnedListField<OspfAreaRegistry, uint32_t, Ospf::AREA_CONFIGS>,
    AtomicField<uint32_t, 100, Ospf::REFERENCE_BANDWIDTH>,
    AtomicField<bool, false, Ospf::BFD>, // TODO:
    AtomicField<bool, true, Ospf::LLS>,
    AtomicField<bool, false, Ospf::OPAQUE>, // TODO:
    AtomicField<bool, false, Ospf::TRANSIT>, // TODO: // virtual link
    OptionalAtomicField<uint32_t, Ospf::DOMAIN_ID>, // TODO:
    ValueField<std::vector<uint32_t>, Ospf::SECONDARY_DOMAIN_ID>, // TODO:
    AtomicField<bool, false, Ospf::DEFAULT_ORIGINATE_ALWAYS>,
    AtomicField<uint32_t, 1, Ospf::DEFAULT_ORIGINATE_METRIC>,
    AtomicField<bool, true, Ospf::DEFAULT_ORIGINATE_METRIC_TYPE>,
    ValueField<std::string, Ospf::DEFAULT_ORIGINATE_ROUTE_MAP>, // TODO:
    OptionalAtomicField<uint32_t, Ospf::DEFAULT_METRIC>, // XXX: REDISTRIBUTION
    AtomicField<bool, true, Ospf::DISCARD_INTERNAL>,
    AtomicField<uint8_t, 110, Ospf::DISCARD_INTERNAL_DISTANCE>,
    AtomicField<bool, true, Ospf::DISCARD_EXTERNAL>,
    AtomicField<uint8_t, 110, Ospf::DISCARD_EXTERNAL_DISTANCE>,
    OptionalAtomicField<std::nullptr_t, Ospf::DISTANCE>, // XXX:
    AtomicField<uint8_t, 110, Ospf::EXTERNAL_DISTANCE>,
    AtomicField<uint8_t, 110, Ospf::INTER_AREA_DISTANCE>,
    AtomicField<uint8_t, 110, Ospf::INTRA_AREA_DISTANCE>,
    OptionalAtomicField<std::nullptr_t, Ospf::DISTRIBUTE_LIST>, // TODO:
    OptionalAtomicField<uint32_t, Ospf::DOMAIN_TAG>, // TODO:
    AtomicField<bool, false, Ospf::EVENT_LOG_ONE_SHOT>, // TODO:
    AtomicField<bool, false, Ospf::EVENT_LOG_PAUSE>, // TODO:
    AtomicField<uint64_t, 0, Ospf::EVENT_LOG_SIZE>, // TODO:
    AtomicField<bool, true, Ospf::IGNORE_MOSPF>, // TODO:
    AtomicField<bool, false, Ospf::SNMP_IFINDEX>, // TODO:
    AtomicField<bool, false, Ospf::ISPF>,
    OptionalAtomicField<uint8_t, Ospf::RETRANSMISSION_DC_LIMIT>,
    OptionalAtomicField<uint8_t, Ospf::RETRANSMISSION_NON_DC_LIMIT>,
    AtomicField<bool, false, Ospf::LOG_ADJACENCY_CHANGES>, // XXX:
    AtomicField<bool, false, Ospf::LOG_ADJACENCY_DETAILS>, // XXX:
    AtomicField<bool, true, Ospf::LRC_FORWARDING_ADDRESS>,
    AtomicField<bool, true, Ospf::LRC_INTER_AREA_SUMMARY>,
    AtomicField<bool, false, Ospf::LRC_NSSA_TRANSLATION>,
    AtomicField<bool, false, Ospf::MAX_METRIC_EXTERNAL>, // XXX: REDISTRIBUTE
    AtomicField<uint32_t, 16711680, Ospf::MAX_METRIC_EXTERNAL_OVERRIDE>, // XXX: REDISTRIBUTE
    AtomicField<bool, false, Ospf::MAX_METRIC_INCLUDE_STUB>,
    OptionalAtomicField<uint16_t, Ospf::MAX_METRIC_ON_STARTUP_TIME>, // XXX: idk or care really
    AtomicField<bool, false, Ospf::MAX_METRIC_ON_STARTUP_WAIT_FOR_BGP>, // XXX: BGP
    AtomicField<bool, false, Ospf::MAX_METRIC_SUMMARY_LSA>,
    OptionalAtomicField<uint32_t, Ospf::MAX_LSA>,
    AtomicField<uint8_t, 75, Ospf::MAX_LSA_THRESHOLD>,
    OptionalAtomicField<uint16_t, Ospf::MAX_LSA_IGNORE_COUNT>,
    AtomicField<uint16_t, 5, Ospf::MAX_LSA_IGNORE_TIME>,
    OptionalAtomicField<uint16_t, Ospf::MAX_LSA_RESET_TIME>,
    AtomicField<bool, false, Ospf::MAX_LSA_WARNING_ONLY>, // XXX:
    AtomicField<uint8_t, 4, Ospf::MAXIMUM_PATHS>,
    ValueField<std::vector<uint32_t>, Ospf::MPLS_LDP_AREAS>, // TODO:
    ValueField<std::vector<uint32_t>, Ospf::MPLS_TRAF_ENG_AREAS>, // TODO:
    ValueField<std::vector<std::tuple<uint32_t, uint32_t>>, Ospf::MPLS_TRAF_ENG_INTERFACES>, // TODO:
    ValueField<std::vector<std::tuple<uint32_t, uint32_t, uint32_t>>, Ospf::MPLS_TRAF_ENG_MESH_GROUP>, // TODO:
    AtomicField<bool, false, Ospf::MPLS_TRAF_ENG_MULTICAST_INACT>, // TODO:
    OptionalAtomicField<uint32_t, Ospf::MPLS_TRAF_ENG_ROUTER_ID>, // TODO:
    ValueField<std::vector<std::tuple<IPPrefix, uint32_t>>, Ospf::NETWORKS,
        OSPF::OspfProcess, OspfSyncNetworks>,
    ValueField<std::vector<std::tuple<
        uint32_t,
        std::optional<uint16_t>,
        std::optional<bool>,
        std::optional<uint16_t>,
        std::optional<uint8_t>
    >>, Ospf::NEIGHBORS,
        OSPF::OspfProcess, OspfSyncNeighbors>,
    AtomicField<bool, false, Ospf::NSF_CISCO_HELPER>, // TODO:
    AtomicField<bool, false, Ospf::NSF_STRICT_CHECKING>, // TODO:
    OptionalAtomicField<uint32_t, Ospf::HELLO_QUEUE_DEPTH>, // XXX:
    ValueField<std::string, Ospf::PREFIX_PRIORITY_ROUTE_MAP>, // TODO:
    OptionalAtomicField<uint32_t, Ospf::UPDATE_QUEUE_DEPTH>, // XXX:
    OptionalAtomicField<uint32_t, Ospf::ROUTER_ID>,
    AtomicField<bool, false, Ospf::SHUTDOWN>, // XXX:
    AtomicField<uint32_t, 1000, Ospf::LSA_ARRIVAL>,
    AtomicField<uint8_t, 33, Ospf::FLOOD_PACING>,
    AtomicField<uint16_t, 240, Ospf::LSA_GROUP_PACING>,
    AtomicField<uint8_t, 66, Ospf::RETRANSMISSION_PACING>,
    ValueField<std::string, Ospf::TABLE_MAP>, // TODO:
    AtomicField<bool, false, Ospf::TABLE_MAP_FILTER>, // TODO:
    AtomicField<uint8_t, 1, Ospf::PRIORITY>,
    OptionalAtomicField<std::nullptr_t, Ospf::REDISTRIBUTE>, // TODO:
    OptionalAtomicField<std::nullptr_t, Ospf::SNMP>, // TODO:
    ValueField<std::vector<std::tuple<IPPrefix, bool, bool, std::optional<uint32_t>>>, Ospf::SUMMARY_ADDRESS,
        OSPF::OspfProcess, OspfSyncSummaries>,
    AtomicField<uint32_t, 0, Ospf::LSA_THROTTLE_DELAY>,
    AtomicField<uint32_t, 5000, Ospf::LSA_THROTTLE_HOLD>,
    AtomicField<uint32_t, 5000, Ospf::LSA_THROTTLE_MAX>,
    AtomicField<uint32_t, 5000, Ospf::SPF_THROTTLE_DELAY>,
    AtomicField<uint32_t, 10000, Ospf::SPF_THROTTLE_HOLD>,
    AtomicField<uint32_t, 10000, Ospf::SPF_THROTTLE_MAX>,
    AtomicField<bool, false, Ospf::TRAFFIC_SHARE_MIN>,
    AtomicField<bool, false, Ospf::TTL_SEC>, // XXX:
    AtomicField<uint8_t, 1, Ospf::TTL_SEC_HOPS> // XXX:
>;

enum class OspfAddressFamilyV3
{
    BASE,
    IPV4,
    IPV6,
    COUNT
};

using OspfAddressFamilyV3Registry = SimpleSubRegistry<__uint128_t, OspfAddressFamilyV3,
    ReferenceContainer<OspfRegistry, OspfAddressFamilyV3::BASE>,
    ReferenceContainer<OspfRegistry, OspfAddressFamilyV3::IPV4>,
    ReferenceContainer<OspfRegistry, OspfAddressFamilyV3::IPV6>
>;

enum class OspfAddressFamilyV2
{
    BASE,
    COUNT
};

using OspfAddressFamilyV2Registry = SimpleSubRegistry<__uint128_t, OspfAddressFamilyV2,
    ReferenceContainer<OspfRegistry, OspfAddressFamilyV2::BASE>
>;
}

#endif // OSPF_REGISTRY_H
