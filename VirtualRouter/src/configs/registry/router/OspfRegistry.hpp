// OspfRegistry.hpp

#ifndef OSPF_REGISTRY_HPP
#define OSPF_REGISTRY_HPP

#include <RegistryTemplate.hpp>
#include <tuple>
#include <IPAddress.hpp>
#include <string>

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

using OspfVirtualLinkRegistry = SubRegistry<__uint128_t, OspfVirtualLink>;

inline __uint128_t generateOspfAreaKey(__uint128_t topoKey, uint32_t areaId)
{
    __uint128_t key = 0;
    key |= __uint128_t(areaId);
    key |= (topoKey & maskU128Bits(99)) << 32;
    return key;
}

enum class OspfArea
{
    AUTHENTICATION_MESSAGE_DIGEST,
    AUTHENTICATION_SPI,
    DEFAULT_EXCLUSION,
    DEFAULT_COST,
    FILTER_LIST,
    NSSA,
    NSSA_METRIC,
    NSSA_METRIC_TYPE,
    NSSA_NO_EXT,
    NSSA_NO_REDISTRIBUTION,
    NSSA_NO_SUMMARY,
    NSSA_ONLY,
    NSSA_ALWAYS_TRANSLATE,
    NSSA_SUPPRESS_FA,
    RANGE,
    STUB_NO_SUMMARY,
    VIRTUAL_LINKS,
    COUNT
};

using OspfAreaRegistry = SubRegistry<__uint128_t, OspfArea,
    AtomicField<bool, false, OspfArea::AUTHENTICATION_MESSAGE_DIGEST>, // TODO
    OptionalAtomicField<uint32_t, OspfArea::AUTHENTICATION_SPI>, // TODO
    AtomicField<bool, false, OspfArea::DEFAULT_EXCLUSION>, // TODO
    OptionalAtomicField<uint32_t, OspfArea::DEFAULT_COST>, // TODO
    OptionalAtomicField<std::nullptr_t, OspfArea::FILTER_LIST>, // TODO
    AtomicField<bool, false, OspfArea::NSSA>, // TODO
    AtomicField<uint32_t, 1, OspfArea::NSSA_METRIC>, // TODO
    AtomicField<bool, true, OspfArea::NSSA_METRIC_TYPE>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_NO_EXT>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_NO_REDISTRIBUTION>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_NO_SUMMARY>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_ONLY>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_ALWAYS_TRANSLATE>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_SUPPRESS_FA>, // TODO
    ValueField<std::vector<std::tuple<IPPrefix, bool, uint32_t>>, OspfArea::RANGE>, // TODO
    AtomicField<bool, false, OspfArea::STUB_NO_SUMMARY>, // TODO
    ValueField<std::vector<std::tuple<>>, OspfArea::VIRTUAL_LINKS> // TODO
>;

inline __uint128_t generateOspfTopologyKey(__uint128_t ospfKey, uint32_t tid)
{
    __uint128_t key = 0;
    key |= __uint128_t(tid);
    key |= (ospfKey & maskU128Bits(67)) << 32;
    return key;
}

enum class OspfTopologyBase
{
    DEFAULT_ORIGINATE_ALWAYS,
    DEFAULT_ORIGINATE_METRIC,
    DEFAULT_ORIGINATE_METRIC_TYPE,
    DEFAULT_ORIGINATE_ROUTE_MAP,
    DEFAULT_METRIC,
    DISCARD_INTERNAL,
    DISCARD_INTERNAL_DISTANCE,
    DISCARD_EXTERNAL,
    DISCARD_EXTERNAL_DISTANCE,
    DISTANCE,
    EXTERNAL_DISTANCE,
    INTER_AREA_DISTANCE,
    INTRA_AREA_DISTANCE,
    DISTRIBUTE_LIST,
    PREFIX_PRIORITY_ROUTE_MAP,
    TABLE_MAP,
    TABLE_MAP_FILTER,
    COUNT
};

using OspfTopologyBaseRegistry = SubRegistry<__uint128_t, OspfTopologyBase,
    AtomicField<bool, false, OspfTopologyBase::DEFAULT_ORIGINATE_ALWAYS>, // TODO
    AtomicField<uint32_t, 1, OspfTopologyBase::DEFAULT_ORIGINATE_METRIC>, // TODO
    AtomicField<bool, true, OspfTopologyBase::DEFAULT_ORIGINATE_METRIC_TYPE>, // TODO
    ValueField<std::string, OspfTopologyBase::DEFAULT_ORIGINATE_ROUTE_MAP>, // TODO
    OptionalAtomicField<uint32_t, OspfTopologyBase::DEFAULT_METRIC>, // TODO
    AtomicField<bool, true, OspfTopologyBase::DISCARD_INTERNAL>, // TODO
    AtomicField<uint8_t, 110, OspfTopologyBase::DISCARD_INTERNAL_DISTANCE>, // TODO
    AtomicField<bool, true, OspfTopologyBase::DISCARD_EXTERNAL>, // TODO
    AtomicField<uint8_t, 110, OspfTopologyBase::DISCARD_EXTERNAL_DISTANCE>, // TODO
    OptionalAtomicField<std::nullptr_t, OspfTopologyBase::DISTANCE>, // TODO
    AtomicField<uint8_t, 110, OspfTopologyBase::EXTERNAL_DISTANCE>,
    AtomicField<uint8_t, 110, OspfTopologyBase::INTER_AREA_DISTANCE>,
    AtomicField<uint8_t, 110, OspfTopologyBase::INTRA_AREA_DISTANCE>,
    OptionalAtomicField<std::nullptr_t, OspfTopologyBase::DISTRIBUTE_LIST>, // TODO
    ValueField<std::string, OspfTopologyBase::PREFIX_PRIORITY_ROUTE_MAP>, // TODO
    ValueField<std::string, OspfTopologyBase::TABLE_MAP>, // TODO
    AtomicField<bool, false, OspfTopologyBase::TABLE_MAP_FILTER> // TODO
>;

enum class OspfTopology
{
    BASE,
    AREA_CONFIGS,   
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
    COUNT
};

using OspfTopologyRegistry = SubRegistry<__uint128_t, OspfTopology,
    ReferenceContainer<OspfTopologyBaseRegistry, OspfTopology::BASE>, // TODO
    OwnedListField<OspfAreaRegistry, OspfTopology::AREA_CONFIGS>, // TODO
    AtomicField<bool, false, OspfTopology::LRC_FORWARDING_ADDRESS>, // TODO
    AtomicField<bool, false, OspfTopology::LRC_INTER_AREA_SUMMARY>, // TODO
    AtomicField<bool, false, OspfTopology::LRC_NSSA_TRANSLATION>, // TODO
    AtomicField<bool, false, OspfTopology::MAX_METRIC_EXTERNAL>, // TODO
    AtomicField<uint32_t, 16711680, OspfTopology::MAX_METRIC_EXTERNAL_OVERRIDE>, // TODO
    AtomicField<bool, false, OspfTopology::MAX_METRIC_INCLUDE_STUB>, // TODO
    OptionalAtomicField<uint16_t, OspfTopology::MAX_METRIC_ON_STARTUP_TIME>, // TODO
    AtomicField<bool, false, OspfTopology::MAX_METRIC_ON_STARTUP_WAIT_FOR_BGP>, // TODO
    AtomicField<bool, false, OspfTopology::MAX_METRIC_SUMMARY_LSA>, // TODO
    OptionalAtomicField<uint32_t, OspfTopology::MAX_LSA>, // TODO
    AtomicField<uint8_t, 75, OspfTopology::MAX_LSA_THRESHOLD>, // TODO
    OptionalAtomicField<uint16_t, OspfTopology::MAX_LSA_IGNORE_COUNT>, // TODO
    AtomicField<uint16_t, 5, OspfTopology::MAX_LSA_IGNORE_TIME>, // TODO
    OptionalAtomicField<uint16_t, OspfTopology::MAX_LSA_RESET_TIME>, // TODO
    AtomicField<bool, false, OspfTopology::MAX_LSA_WARNING_ONLY>, // TODO
    AtomicField<uint8_t, 4, OspfTopology::MAXIMUM_PATHS>, // TODO
    ValueField<std::vector<uint32_t>, OspfTopology::MPLS_LDP_AREAS>, // TODO
    ValueField<std::vector<uint32_t>, OspfTopology::MPLS_TRAF_ENG_AREAS>, // TODO
    ValueField<std::vector<std::tuple<uint32_t, uint32_t>>, OspfTopology::MPLS_TRAF_ENG_INTERFACES>, // TODO
    ValueField<std::vector<std::tuple<uint32_t, uint32_t, uint32_t>>, OspfTopology::MPLS_TRAF_ENG_MESH_GROUP>, // TODO
    AtomicField<bool, false, OspfTopology::MPLS_TRAF_ENG_MULTICAST_INACT>, // TODO
    OptionalAtomicField<uint32_t, OspfTopology::MPLS_TRAF_ENG_ROUTER_ID>, // TODO
    ValueField<std::vector<std::tuple<IPPrefix, uint32_t>>, OspfTopology::NETWORKS>, // TODO
    ValueField<std::vector<std::tuple<IPAddress, uint16_t>>, OspfTopology::NEIGHBORS>, // TODO
    AtomicField<bool, false, OspfTopology::NSF_CISCO_HELPER>, // TODO
    AtomicField<bool, false, OspfTopology::NSF_STRICT_CHECKING>, // TODO
    AtomicField<uint8_t, 1, OspfTopology::PRIORITY>, // TODO
    OptionalAtomicField<std::nullptr_t, OspfTopology::REDISTRIBUTE>, // TODO
    OptionalAtomicField<std::nullptr_t, OspfTopology::SNMP>, // TODO
    ValueField<std::vector<std::tuple<IPPrefix, bool, bool>>, OspfTopology::SUMMARY_ADDRESS>, // TODO
    AtomicField<uint32_t, 0, OspfTopology::LSA_THROTTLE_DELAY>, // TODO
    AtomicField<uint32_t, 5000, OspfTopology::LSA_THROTTLE_HOLD>, // TODO
    AtomicField<uint32_t, 5000, OspfTopology::LSA_THROTTLE_MAX>, // TODO
    AtomicField<uint32_t, 5000, OspfTopology::SPF_THROTTLE_DELAY>, // TODO
    AtomicField<uint32_t, 10000, OspfTopology::SPF_THROTTLE_HOLD>, // TODO
    AtomicField<uint32_t, 10000, OspfTopology::SPF_THROTTLE_MAX>, // TODO
    AtomicField<bool, false, OspfTopology::TRAFFIC_SHARE_MIN>, // TODO
    AtomicField<bool, false, OspfTopology::TTL_SEC>, // TODO
    AtomicField<uint8_t, 1, OspfTopology::TTL_SEC_HOPS> // TODO
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
    BASE, 
    REFERENCE_BANDWIDTH,
    BFD,
    LLS,
    OPAQUE,
    TRANSIT,
    DOMAIN_ID,
    SECONDARY_DOMAIN_ID,
    DOMAIN_TAG,
    EVENT_LOG_ONE_SHOT,
    EVENT_LOG_PAUSE,
    EVENT_LOG_SIZE,
    IGNORE_MOSPF,
    SNMP_IFINDEX,
    ISPF,
    DC_LIMIT,
    NON_DC_LIMIT,
    LOG_ADJACENCY_CHANGES,
    LOG_ADJACENCY_DETAILS,
    HELLO_QUEUE_DEPTH,
    UPDATE_QUEUE_DEPTH,
    ROUTER_ID,
    SHUTDOWN,
    LSA_ARRIVAL,
    FLOOD_PACING,
    LSA_GROUP_PACING,
    RETRANSMISSION_PACING,
    COUNT,
};

using OspfRegistry = SubRegistry<__uint128_t, Ospf,
    ReferenceContainer<OspfTopologyRegistry, Ospf::BASE>, // TODO
    AtomicField<uint32_t, 100, Ospf::REFERENCE_BANDWIDTH>, // TODO
    AtomicField<bool, false, Ospf::BFD>, // TODO
    AtomicField<bool, true, Ospf::LLS>, // TODO
    AtomicField<bool, false, Ospf::OPAQUE>, // TODO
    AtomicField<bool, false, Ospf::TRANSIT>, // TODO
    OptionalAtomicField<uint32_t, Ospf::DOMAIN_ID>, // TODO
    ValueField<std::vector<uint32_t>, Ospf::SECONDARY_DOMAIN_ID>, // TODO
    OptionalAtomicField<uint32_t, Ospf::DOMAIN_TAG>, // TODO
    AtomicField<bool, false, Ospf::EVENT_LOG_ONE_SHOT>, // TODO
    AtomicField<bool, false, Ospf::EVENT_LOG_PAUSE>, // TODO
    AtomicField<uint64_t, 0, Ospf::EVENT_LOG_SIZE>, // TODO
    AtomicField<bool, true, Ospf::IGNORE_MOSPF>, // TODO
    AtomicField<bool, false, Ospf::SNMP_IFINDEX>, // TODO
    AtomicField<bool, false, Ospf::ISPF>, // TODO
    OptionalAtomicField<uint8_t, Ospf::DC_LIMIT>, // TODO
    OptionalAtomicField<uint8_t, Ospf::NON_DC_LIMIT>, // TODO
    AtomicField<bool, false, Ospf::LOG_ADJACENCY_CHANGES>, // TODO
    AtomicField<bool, false, Ospf::LOG_ADJACENCY_DETAILS>, // TODO
    OptionalAtomicField<uint32_t, Ospf::HELLO_QUEUE_DEPTH>, // TODO
    OptionalAtomicField<uint32_t, Ospf::UPDATE_QUEUE_DEPTH>, // TODO
    OptionalAtomicField<uint32_t, Ospf::ROUTER_ID>, // TODO
    AtomicField<bool, false, Ospf::SHUTDOWN>, // TODO
    AtomicField<uint32_t, 1000, Ospf::LSA_ARRIVAL>, // TODO
    AtomicField<uint8_t, 33, Ospf::FLOOD_PACING>, // TODO
    AtomicField<uint16_t, 240, Ospf::LSA_GROUP_PACING>, // TODO
    AtomicField<uint8_t, 66, Ospf::RETRANSMISSION_PACING> // TODO
>;

enum class OspfAddressFamilyV3
{
    BASE,
    IPV4,
    IPV6,
    COUNT
};

using OspfAddressFamilyV3Registry = SubRegistry<__uint128_t, OspfAddressFamilyV3,
    ReferenceContainer<OspfRegistry, OspfAddressFamilyV3::BASE>, // TODO
    ReferenceContainer<OspfRegistry, OspfAddressFamilyV3::IPV4>, // TODO masked of base
    ReferenceContainer<OspfRegistry, OspfAddressFamilyV3::IPV6> // TODO masked of base
>;

enum class OspfAddressFamilyV2
{
    BASE,
    SNMP,
    TOPOLOGIES,
    COUNT
};

using OspfAddressFamilyV2Registry = SubRegistry<__uint128_t, OspfAddressFamilyV2,
    ReferenceContainer<OspfRegistry, OspfAddressFamilyV2::BASE>, // TODO
    OptionalAtomicField<std::nullptr_t, OspfAddressFamilyV2::SNMP>, // TODO
    OwnedListField<OspfTopologyRegistry, OspfAddressFamilyV2::TOPOLOGIES> // TODO
>;
}

#endif // OSPF_REGISTRY_HPP
