// OspfInterfaceRegistry.hpp

#ifndef OSPF_INTERFACE_REGISTRY_HPP
#define OSPF_INTERFACE_REGISTRY_HPP

#include <RegistryReference.hpp>
#include <SubRegistry.hpp>
#include <AddressFamily.hpp>
#include <HeaderHelpers.hpp>
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
inline uint64_t generateOspfInterfaceKey(uint32_t ifaceId, AddressFamily af, bool isV3) {
    uint8_t addressFamily = af == AddressFamily::NONE ? 0
        : af == AddressFamily::IPv4 ? 1 : 2;
    uint64_t k = 0;    
    k |= uint64_t(ifaceId) & maskU64Bits(32);
    k |= (uint64_t(addressFamily) & maskU64Bits(2)) << 32;
    k |= (uint64_t(isV3 ? 1u : 0u) & maskU64Bits(1)) < 34;
    return k;
}

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

using OspfInterfaceRegistry = SubRegistry<uint64_t, OspfInterface,
    AtomicField<bool, false, OspfInterface::BFD>,
    AtomicField<uint16_t, 1, OspfInterface::COST>,
    AtomicField<bool, false, OspfInterface::DATABASE_FILTER>,
    AtomicField<uint16_t, 30, OspfInterface::DEAD_INTERVAL>,
    AtomicField<bool, false, OspfInterface::DEMAND_CIRCUIT>,
    AtomicField<bool, false, OspfInterface::FLOOD_REDUCTION>,
    AtomicField<uint16_t, 10, OspfInterface::HELLO_INTERVAL>,
    AtomicField<uint8_t, 1, OspfInterface::HELLO_MULTIPLIER>,
    AtomicField<bool, false, OspfInterface::MTU_IGNORE>,
    ValueField<std::vector<std::tuple<
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

enum class OspfInterfaceAddressFamily : uint8_t
{
    BASE,
    IPV4,
    IPV6,
    COUNT,
};

using OspfInterfaceAddressFamilyRegistry = SubRegistry<uint64_t, OspfInterfaceAddressFamily,
    ReferenceContainer<OspfInterfaceRegistry, OspfInterfaceAddressFamily::BASE>,
    ReferenceContainer<OspfInterfaceRegistry, OspfInterfaceAddressFamily::IPV4>,
    ReferenceContainer<OspfInterfaceRegistry, OspfInterfaceAddressFamily::IPV6>
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

using OspfInterfaceBaseRegistry = SubRegistry<uint64_t, OspfInterfaceBase,
    ReferenceContainer<OspfInterfaceRegistry, OspfInterfaceBase::BASE>,
    OwnedListField<OspfInterfaceAddressFamilyRegistry, OspfInterfaceBase::PROCESS_CONFIGS>,
    OptionalAtomicField<uint16_t, OspfInterfaceBase::PROCESS_ID>,
    OptionalAtomicField<uint32_t, OspfInterfaceBase::AREA_ID>,
    AtomicField<bool, true, OspfInterfaceBase::INCLUDE_SECONDARIES>,
    AtomicField<bool, false, OspfInterfaceBase::AUTHENTICATION_MESSAGE_DIGEST>,
    OptionalAtomicField<bool, OspfInterfaceBase::AUTHENTICATION_ENCRYPT>,
    ValueField<std::string, OspfInterfaceBase::AUTHENTICATION_KEY>,
    AtomicField<uint32_t, 0, OspfInterfaceBase::AUTHENTICATION_SPI>,
    AtomicField<bool, true, OspfInterfaceBase::AUTHENTICATION_NULL>,
    AtomicField<bool, true, OspfInterfaceBase::LLS>,
    OptionalAtomicField<uint8_t, OspfInterfaceBase::MESSAGE_DIGEST_KEY_ID>,
    ValueField<std::string, OspfInterfaceBase::MESSAGE_DIGEST_KEY>,
    AtomicField<bool, false, OspfInterfaceBase::MESSAGE_DIGEST_ENCRRYPT>,
    AtomicField<bool, false, OspfInterfaceBase::PREFIX_SUPPRESSION>,
    AtomicField<uint16_t, 5, OspfInterfaceBase::RESYNC_TIMEOUT>,
    AtomicField<bool, false, OspfInterfaceBase::SHUTDOWN>
>;
}

#endif // OSPF_INTERFACE_REGISTRY_HPP
