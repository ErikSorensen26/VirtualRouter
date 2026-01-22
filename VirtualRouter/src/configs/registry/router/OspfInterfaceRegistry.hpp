// OspfInterfaceRegistry.hpp

#ifndef OSPF_INTERFACE_REGISTRY_HPP
#define OSPF_INTERFACE_REGISTRY_HPP

#include <RegistryReference.hpp>
#include <SubRegistry.hpp>
#include <optional>
#include <tuple>
#include <string>

namespace OSPF
{
enum class NetworkType : uint8_t    
{
    BROADCAST,
    NON_BROADCAST,
    POINT_TO_MULTIPOINT,
    POINT_TO_MULTIPOINT_BROADCAST,
    POINT_TO_POINT
};
}

namespace Config
{

enum class OspfNeighbor : uint8_t
{
    ADDRESS,
    COST,
    POLL_INTERVAL,
    PRIORITY
};

enum class OspfInterface : uint8_t
{
    BFD,
    COST,
    DATABASE_FILTER,
    DEAD_INTERVAL,
    DEMAND_CIRCUIT,
    FLOOD_REDUCTION,
    HELLO_INTERVAL,
    HELLO_MULTIPLIER,
    MTU_IGNORE,
    NEIGHBOR,
    NETWORK,
    PRIORITY,
    RETRANSMIT_INTERVAL,
    TRANSMIT_DELAY,
    COUNT
};

using OspfInterfaceRegistry = SubRegistry<OspfInterface,
    AtomicField<bool, false, OspfInterface::BFD>,
    AtomicField<uint16_t, 1, OspfInterface::COST>,
    AtomicField<bool, false, OspfInterface::DATABASE_FILTER>,
    AtomicField<uint16_t, 30, OspfInterface::DEAD_INTERVAL>,
    AtomicField<bool, false, OspfInterface::DEMAND_CIRCUIT>,
    AtomicField<bool, false, OspfInterface::FLOOD_REDUCTION>,
    AtomicField<uint16_t, 10, OspfInterface::HELLO_INTERVAL>,
    AtomicField<uint8_t, 1, OspfInterface::HELLO_MULTIPLIER>,
    AtomicField<bool, false, OspfInterface::MTU_IGNORE>,
    VariableField<std::vector<std::tuple<
        __uint128_t,
        std::optional<uint16_t>,
        std::optional<bool>,
        std::optional<uint16_t>,
        std::optional<uint8_t
    >>>, OspfInterface::NEIGHBOR>,
    AtomicField<OSPF::NetworkType, OSPF::NetworkType::BROADCAST, OspfInterface::NETWORK>,
    AtomicField<uint8_t, 1, OspfInterface::PRIORITY>,
    AtomicField<uint16_t, 5, OspfInterface::RETRANSMIT_INTERVAL>,
    AtomicField<uint16_t, 1, OspfInterface::TRANSMIT_DELAY>
>;

using OspfInterfaceRegistryMask = MaskSubRegistry<OspfInterfaceRegistry>;

enum class OspfInterfaceAddressFamily : uint8_t
{
    BASE,
    IPV4,
    IPV6,
    COUNT,
};

using OspfInterfaceAddressFamilyRegistry = SubRegistry<OspfInterfaceAddressFamily,
    ReferenceContainer<OspfInterface, OspfInterfaceRegistry, OspfInterfaceAddressFamily::BASE>,
    ReferenceContainer<OspfInterface, OspfInterfaceRegistryMask, OspfInterfaceAddressFamily::BASE>,
    ReferenceContainer<OspfInterface, OspfInterfaceRegistryMask, OspfInterfaceAddressFamily::BASE>
>;

enum class OspfInterfaceBase : uint8_t
{
    BASE,
    PROCESS_CONFIGS,
    PROCESS_ID,
    AREA_ID,
    INCLUDE_SECONDARIES,
    AUTHENTICATION_MESSAGE_DIGEST,
    AUTHENTICATION_ENCRYPT,
    AUTHENTICATION_KEY,
    AUTHENTICATION_SPI,
    AUTHENTICATION_NULL,
    LLS,
    MESSAGE_DIGEST_KEY_ID,
    MESSAGE_DIGEST_KEY,
    MESSAGE_DIGEST_ENCRRYPT,
    PREFIX_SUPPRESSION,
    RESYNC_TIMEOUT,
    SHUTDOWN,
    COUNT
};

using OspfInterfaceBaseRegistry = SubRegistry<OspfInterfaceBase,
    ReferenceContainer<OspfInterface, OspfInterfaceRegistry, OspfInterfaceBase::BASE>,
    VariableField<std::vector<Reference<OspfInterfaceAddressFamily, OspfInterfaceAddressFamilyRegistry>>, OspfInterfaceBase::PROCESS_CONFIGS>,
    UnsetAtomicField<uint16_t, OspfInterfaceBase::PROCESS_ID>,
    UnsetAtomicField<uint32_t, OspfInterfaceBase::AREA_ID>,
    AtomicField<bool, true, OspfInterfaceBase::INCLUDE_SECONDARIES>,
    AtomicField<bool, false, OspfInterfaceBase::AUTHENTICATION_MESSAGE_DIGEST>,
    UnsetAtomicField<bool, OspfInterfaceBase::AUTHENTICATION_ENCRYPT>,
    VariableField<std::string, OspfInterfaceBase::AUTHENTICATION_KEY>,
    AtomicField<uint32_t, 0, OspfInterfaceBase::AUTHENTICATION_SPI>,
    AtomicField<bool, true, OspfInterfaceBase::AUTHENTICATION_NULL>,
    AtomicField<bool, true, OspfInterfaceBase::LLS>,
    UnsetAtomicField<uint8_t, OspfInterfaceBase::MESSAGE_DIGEST_KEY_ID>,
    VariableField<std::string, OspfInterfaceBase::MESSAGE_DIGEST_KEY>,
    AtomicField<bool, false, OspfInterfaceBase::MESSAGE_DIGEST_ENCRRYPT>,
    AtomicField<bool, false, OspfInterfaceBase::PREFIX_SUPPRESSION>,
    AtomicField<uint16_t, 5, OspfInterfaceBase::RESYNC_TIMEOUT>,
    AtomicField<bool, false, OspfInterfaceBase::SHUTDOWN>
>;
}

#endif // OSPF_INTERFACE_REGISTRY_HPP
