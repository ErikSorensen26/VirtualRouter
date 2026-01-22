// OspfRegistry.hpp

#ifndef OSPF_REGISTRY_HPP
#define OSPF_REGISTRY_HPP

#include <RegistryReference.hpp>
#include <SubRegistry.hpp>
#include <tuple>
#include <IPAddress.hpp>
#include <string>

namespace OSPF
{
}

namespace Config
{
enum class OspfVirtualLink
{
    COUNT
};

using OspfVirtualLinkRegistry = SubRegistry<OspfVirtualLink>;

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

using OspfAreaRegistry = SubRegistry<OspfArea,
    AtomicField<bool, false, OspfArea::AUTHENTICATION_MESSAGE_DIGEST>, // TODO
    UnsetAtomicField<uint32_t, OspfArea::AUTHENTICATION_SPI>, // TODO
    AtomicField<bool, false, OspfArea::DEFAULT_EXCLUSION>, // TODO
    AtomicField<uint32_t, 1, OspfArea::DEFAULT_COST>, // TODO
    UnsetAtomicField<void, OspfArea::FILTER_LIST>, // TODO
    AtomicField<bool, false, OspfArea::NSSA>, // TODO
    AtomicField<uint32_t, 1, OspfArea::NSSA_METRIC>, // TODO
    AtomicField<bool, true, OspfArea::NSSA_METRIC_TYPE>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_NO_EXT>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_NO_REDISTRIBUTION>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_NO_SUMMARY>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_ONLY>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_ALWAYS_TRANSLATE>, // TODO
    AtomicField<bool, false, OspfArea::NSSA_SUPPRESS_FA>, // TODO
    VariableField<std::vector<std::tuple<IPPrefix, bool, uint32_t>>, OspfArea::RANGE>, // TODO
    AtomicField<bool, false, OspfArea::STUB_NO_SUMMARY>, // TODO
    VariableField<std::vector<std::tuple<>>, OspfArea::VIRTUAL_LINKS> // TODO
>;

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
    COUNT
};

using OspfTopologyBaseRegistry = SubRegistry<OspfTopologyBase,
    AtomicField<bool, false, OspfTopologyBase::DEFAULT_ORIGINATE_ALWAYS>, // TODO
    AtomicField<uint32_t, 1, OspfTopologyBase::DEFAULT_ORIGINATE_METRIC>, // TODO
    AtomicField<bool, true, OspfTopologyBase::DEFAULT_ORIGINATE_METRIC_TYPE>, // TODO
    VariableField<std::string, OspfTopologyBase::DEFAULT_ORIGINATE_ROUTE_MAP>, // TODO
    UnsetAtomicField<uint32_t, OspfTopologyBase::DEFAULT_METRIC>, // TODO
    AtomicField<bool, true, OspfTopologyBase::DISCARD_INTERNAL>, // TODO
    AtomicField<uint8_t, 110, OspfTopologyBase::DISCARD_INTERNAL_DISTANCE>, // TODO
    AtomicField<bool, true, OspfTopologyBase::DISCARD_EXTERNAL>, // TODO
    AtomicField<uint8_t, 110, OspfTopologyBase::DISCARD_EXTERNAL_DISTANCE>, // TODO
    UnsetAtomicField<void, OspfTopologyBase::DISTANCE>, // TODO
    AtomicField<uint8_t, 110, OspfTopologyBase::EXTERNAL_DISTANCE>, // TODO
    AtomicField<uint8_t, 110, OspfTopologyBase::INTER_AREA_DISTANCE>, // TODO
    AtomicField<uint8_t, 110, OspfTopologyBase::INTRA_AREA_DISTANCE>, // TODO
    UnsetAtomicField<void, OspfTopologyBase::DISTRIBUTE_LIST> // TODO
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
    MAXIMUM_PATHS,
    NEIGHBORS,
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
    COUNT
};

using OspfTopologyRegistry = SubRegistry<OspfTopology,
    ReferenceContainer<OspfTopologyBase, OspfTopologyBaseRegistry, OspfTopology::BASE>, // TODO
    VariableField<std::vector<Reference<OspfArea, OspfAreaRegistry>>, OspfTopology::AREA_CONFIGS>, // TODO
    AtomicField<bool, false, OspfTopology::LRC_FORWARDING_ADDRESS>, // TODO
    AtomicField<bool, false, OspfTopology::LRC_INTER_AREA_SUMMARY>, // TODO
    AtomicField<bool, false, OspfTopology::LRC_NSSA_TRANSLATION>, // TODO
    AtomicField<bool, false, OspfTopology::MAX_METRIC_EXTERNAL>, // TODO
    AtomicField<uint32_t, 16711680, OspfTopology::MAX_METRIC_EXTERNAL_OVERRIDE>, // TODO
    AtomicField<bool, false, OspfTopology::MAX_METRIC_INCLUDE_STUB>, // TODO
    UnsetAtomicField<uint16_t, OspfTopology::MAX_METRIC_ON_STARTUP_TIME>, // TODO
    AtomicField<bool, false, OspfTopology::MAX_METRIC_ON_STARTUP_WAIT_FOR_BGP>, // TODO
    AtomicField<bool, false, OspfTopology::MAX_METRIC_SUMMARY_LSA>, // TODO
    AtomicField<uint8_t, 1, OspfTopology::MAXIMUM_PATHS>, // TODO
    VariableField<std::vector<std::tuple<IPAddress, uint16_t>>, OspfTopology::NEIGHBORS>, // TODO
    AtomicField<uint8_t, 1, OspfTopology::PRIORITY>, // TODO
    UnsetAtomicField<void, OspfTopology::REDISTRIBUTE>, // TODO
    UnsetAtomicField<void, OspfTopology::SNMP>, // TODO
    VariableField<std::vector<std::tuple<IPPrefix, bool, bool>>, OspfTopology::SUMMARY_ADDRESS>, // TODO
    AtomicField<uint32_t, 0, OspfTopology::LSA_THROTTLE_DELAY>, // TODO
    AtomicField<uint32_t, 5000, OspfTopology::LSA_THROTTLE_HOLD>, // TODO
    AtomicField<uint32_t, 5000, OspfTopology::LSA_THROTTLE_MAX>, // TODO
    AtomicField<uint32_t, 5000, OspfTopology::SPF_THROTTLE_DELAY>, // TODO
    AtomicField<uint32_t, 10000, OspfTopology::SPF_THROTTLE_HOLD>, // TODO
    AtomicField<uint32_t, 10000, OspfTopology::SPF_THROTTLE_MAX>, // TODO
    AtomicField<bool, false, OspfTopology::TRAFFIC_SHARE_MIN> // TODO
>;

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
    MPLS_LDP_AREAS,
    MPLS_TRAF_ENG_AREAS,
    MPLS_TRAF_ENG_INTERFACES,
    MPLS_TRAF_ENG_MESH_GROUP,
    MPLS_TRAF_ENG_MULTICAST_INACT,
    MPLS_TRAF_ENG_ROUTER_ID,
    MAX_LSA,
    MAX_LSA_THRESHOLD,
    MAX_LSA_IGNORE_COUNT,
    MAX_LSA_IGNORE_TIME,
    MAX_LSA_RESET_TIME,
    MAX_LSA_WARNING_ONLY,
    NETWORKS,
    NSF_CISCO_HELPER,
    NSF_STRICT_CHECKING,
    PREFIX_PRIORITY_ROUTE_MAP,
    HELLO_QUEUE_DEPTH,
    UPDATE_QUEUE_DEPTH,
    ROUTER_ID,
    SHUTDOWN,
    LSA_ARRIVAL,
    FLOOD_PACING,
    LSA_GROUP_PACING,
    RETRANSMISSION_PACING,
    TABLE_MAP,
    TABLE_MAP_FILTER,
    TTL_SEC,
    TTL_SEC_HOPS,
    COUNT,
};

using OspfRegistry = SubRegistry<Ospf,
    ReferenceContainer<OspfTopology, OspfTopologyRegistry, Ospf::BASE>, // TODO
    AtomicField<uint32_t, 100, Ospf::REFERENCE_BANDWIDTH>, // TODO
    AtomicField<bool, false, Ospf::BFD>, // TODO
    AtomicField<bool, true, Ospf::LLS>, // TODO
    AtomicField<bool, false, Ospf::OPAQUE>, // TODO
    AtomicField<bool, false, Ospf::TRANSIT>, // TODO
    UnsetAtomicField<uint32_t, Ospf::DOMAIN_ID>, // TODO
    VariableField<std::vector<uint32_t>, Ospf::SECONDARY_DOMAIN_ID>, // TODO
    UnsetAtomicField<uint32_t, Ospf::DOMAIN_TAG>, // TODO
    AtomicField<bool, false, Ospf::EVENT_LOG_ONE_SHOT>, // TODO
    AtomicField<bool, false, Ospf::EVENT_LOG_PAUSE>, // TODO
    AtomicField<uint64_t, false, Ospf::EVENT_LOG_SIZE>, // TODO
    AtomicField<bool, true, Ospf::IGNORE_MOSPF>, // TODO
    AtomicField<bool, false, Ospf::SNMP_IFINDEX>, // TODO
    AtomicField<bool, false, Ospf::ISPF>, // TODO
    UnsetAtomicField<uint8_t, Ospf::DC_LIMIT>, // TODO
    UnsetAtomicField<uint8_t, Ospf::NON_DC_LIMIT>, // TODO
    AtomicField<bool, false, Ospf::LOG_ADJACENCY_CHANGES>, // TODO
    AtomicField<bool, false, Ospf::LOG_ADJACENCY_DETAILS>, // TODO
    VariableField<std::vector<uint32_t>, Ospf::MPLS_LDP_AREAS>, // TODO
    VariableField<std::vector<uint32_t>, Ospf::MPLS_TRAF_ENG_AREAS>, // TODO
    VariableField<std::vector<std::tuple<uint32_t, uint32_t>>, Ospf::MPLS_TRAF_ENG_INTERFACES>, // TODO
    VariableField<std::vector<std::tuple<uint32_t, uint32_t, uint32_t>>, Ospf::MPLS_TRAF_ENG_MESH_GROUP>, // TODO
    AtomicField<bool, false, Ospf::MPLS_TRAF_ENG_MULTICAST_INACT>, // TODO
    UnsetAtomicField<uint32_t, Ospf::MPLS_TRAF_ENG_ROUTER_ID>, // TODO
    UnsetAtomicField<uint32_t, Ospf::MAX_LSA>, // TODO
    AtomicField<uint8_t, 75, Ospf::MAX_LSA_THRESHOLD>, // TODO
    UnsetAtomicField<uint16_t, Ospf::MAX_LSA_IGNORE_COUNT>, // TODO
    AtomicField<uint16_t, 5, Ospf::MAX_LSA_IGNORE_TIME>, // TODO
    UnsetAtomicField<uint16_t, Ospf::MAX_LSA_RESET_TIME>, // TODO
    AtomicField<bool, false, Ospf::MAX_LSA_WARNING_ONLY>, // TODO
    VariableField<std::vector<std::tuple<IPPrefix, uint32_t>>, Ospf::NETWORKS>, // TODO
    AtomicField<bool, false, Ospf::NSF_CISCO_HELPER>, // TODO
    AtomicField<bool, false, Ospf::NSF_STRICT_CHECKING>, // TODO
    VariableField<std::string, Ospf::PREFIX_PRIORITY_ROUTE_MAP>, // TODO
    UnsetAtomicField<uint32_t, Ospf::HELLO_QUEUE_DEPTH>, // TODO
    UnsetAtomicField<uint32_t, Ospf::UPDATE_QUEUE_DEPTH>, // TODO
    UnsetAtomicField<uint32_t, Ospf::ROUTER_ID>, // TODO
    AtomicField<bool, false, Ospf::SHUTDOWN>, // TODO
    AtomicField<uint32_t, 1000, Ospf::LSA_ARRIVAL>, // TODO
    AtomicField<uint8_t, 33, Ospf::FLOOD_PACING>, // TODO
    AtomicField<uint16_t, 240, Ospf::LSA_GROUP_PACING>, // TODO
    AtomicField<uint8_t, 66, Ospf::RETRANSMISSION_PACING>, // TODO
    VariableField<std::string, Ospf::TABLE_MAP>, // TODO
    AtomicField<bool, false, Ospf::TABLE_MAP_FILTER>, // TODO
    AtomicField<bool, false, Ospf::TTL_SEC>, // TODO
    AtomicField<uint8_t, 1, Ospf::TTL_SEC_HOPS> // TODO
>;

using OspfRegistryMask = MaskSubRegistry<OspfRegistry>;

enum class OspfAddressFamilyV3
{
    BASE,
    IPV4,
    IPV6
};

using OspfAddressFamilyV3Registry = SubRegistry<OspfAddressFamilyV3,
    ReferenceContainer<Ospf, OspfRegistry, OspfAddressFamilyV3::BASE>, // TODO
    ReferenceContainer<Ospf, OspfRegistryMask, OspfAddressFamilyV3::IPV4>, // TODO
    ReferenceContainer<Ospf, OspfRegistryMask, OspfAddressFamilyV3::IPV6> // TODO
>;

enum class OspfAddressFamilyV2
{
    SNMP,
    TOPOLOGIES,
    COUNT
};

using OspfAddressFamilyV2Registry = SubRegistry<OspfAddressFamilyV2,
    UnsetAtomicField<void, OspfAddressFamilyV2::SNMP>, // TODO
    VariableField<std::vector<Reference<OspfTopology, OspfTopologyRegistry>>, OspfAddressFamilyV2::TOPOLOGIES> // TODO
>;
}

#endif // OSPF_REGISTRY_HPP
