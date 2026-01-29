// OspfInterfaceRegistry.hpp

#ifndef OSPF_INTERFACE_REGISTRY_HPP
#define OSPF_INTERFACE_REGISTRY_HPP

#include <RegistryTemplate.hpp>
#include <optional>
#include <vector>
#include <IPAddress.hpp>
#include <AddressFamily.hpp>
#include <HeaderHelpers.hpp>

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

enum class AuthType : uint8_t
{
    NULL_AUTH = 0,
    SIMPLE = 1,
    CRYPTO = 2
};

enum class IPsecAuthType : uint8_t
{
    NULL_AUTH,
    MD5,
    SHA1
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

inline __uint128_t generateOspfAfInterfaceKey(uint32_t ifaceId, uint32_t procId, AddressFamily af, bool isV3) {
    uint8_t addressFamily = af == AddressFamily::NONE ? 0
        : af == AddressFamily::IPv4 ? 1 : 2;
    uint64_t k = 0;    
    k |= uint64_t(ifaceId) & maskU64Bits(32);
    k |= (uint64_t(addressFamily) & maskU64Bits(2)) << 32;
    k |= (uint64_t(isV3 ? 1u : 0u) & maskU64Bits(1)) < 34;
    k |= (uint64_t(procId) & maskU64Bits(32)) << 35;
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
    PASSIVE,
    RETRANSMIT_INTERVAL,
    TRANSMIT_DELAY,
    COUNT
};

using OspfInterfaceRegistry = SubRegistry<__uint128_t, OspfInterface,
    AtomicField<bool, false, OspfInterface::BFD>, // TODO
    OptionalAtomicField<uint16_t, OspfInterface::COST>,
    AtomicField<bool, false, OspfInterface::DATABASE_FILTER>,
    OptionalAtomicField<uint16_t, OspfInterface::DEAD_INTERVAL>, // edit hello interval on interface
    AtomicField<bool, false, OspfInterface::DEMAND_CIRCUIT>,
    AtomicField<bool, false, OspfInterface::FLOOD_REDUCTION>,
    OptionalAtomicField<uint16_t, OspfInterface::HELLO_INTERVAL>, // edit hello interval on interface
    OptionalAtomicField<uint8_t, OspfInterface::HELLO_MULTIPLIER>, // edit hello interval on interface
    AtomicField<bool, false, OspfInterface::MTU_IGNORE>,
    ValueField<std::vector<std::tuple<
        IPAddress,
        std::optional<uint16_t>,
        std::optional<bool>,
        std::optional<uint16_t>,
        std::optional<uint8_t
    >>>, OspfInterface::NEIGHBOR>,
    AtomicField<OSPF::NetworkType, OSPF::NetworkType::BROADCAST, OspfInterface::NETWORK>, // update neighbors, timers, and multicast capability
    AtomicField<uint8_t, 1, OspfInterface::PRIORITY>,
    AtomicField<bool, false, OspfInterface::PASSIVE>,
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

using OspfInterfaceAddressFamilyRegistry = SubRegistry<__uint128_t, OspfInterfaceAddressFamily,
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
    AUTHENTICATION_TYPE,
    AUTHENTICATION_ENCRYPT,
    AUTHENTICATION_KEY,
    AUTHENTICATION_IPSEC,
    LLS,
    MESSAGE_DIGEST_KEY,
    MESSAGE_DIGEST_KEY_ID,
    MESSAGE_DIGEST_KEYS,
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
    OptionalAtomicField<OSPF::AuthType, OspfInterfaceBase::AUTHENTICATION_TYPE>,
    OptionalAtomicField<bool, OspfInterfaceBase::AUTHENTICATION_ENCRYPT>,
    OptionalAtomicField<uint64_t, OspfInterfaceBase::AUTHENTICATION_KEY>,
    ValueField<std::tuple<
        OSPF::IPsecAuthType, // Type enabled
        std::tuple<uint32_t, OSPF::IPsecAuthType, std::array<uint8_t, 40>
    >>, OspfInterfaceBase::AUTHENTICATION_IPSEC>, // TODO
    AtomicField<bool, true, OspfInterfaceBase::LLS>,
    OptionalAtomicField<__uint128_t, OspfInterfaceBase::MESSAGE_DIGEST_KEY>, // place up to date key when keys change
    OptionalAtomicField<uint8_t, OspfInterfaceBase::MESSAGE_DIGEST_KEY_ID>, 
    ValueField<std::vector<std::tuple<uint8_t, std::array<uint8_t, 16>, uint64_t>>, OspfInterfaceBase::MESSAGE_DIGEST_KEYS>,
    AtomicField<bool, false, OspfInterfaceBase::MESSAGE_DIGEST_ENCRRYPT>,
    AtomicField<bool, false, OspfInterfaceBase::PREFIX_SUPPRESSION>,
    AtomicField<uint16_t, 5, OspfInterfaceBase::RESYNC_TIMEOUT>, // TODO 
    AtomicField<bool, false, OspfInterfaceBase::SHUTDOWN> // TODO
>;
}

#endif // OSPF_INTERFACE_REGISTRY_HPP
