// OspfInterfaceRegistry.h

#ifndef OSPF_INTERFACE_REGISTRY_H
#define OSPF_INTERFACE_REGISTRY_H

#include <optional>
#include <vector>

#include "IPAddress.hpp"
#include "AddressFamily.hpp"
#include "packet/HeaderHelpers.hpp"
#include "configs/SubRegistry.hpp"
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryReference.hpp"

namespace OSPF
{
class OspfInterface;

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

enum class IPsecEncryptType : uint8_t
{
    _3DES,
    AES_CBC_128,
    AES_CBC_192,
    AES_CBC_256,
    DES,
    NULL_TYPE
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
    TTL_SEC,
    TTL_SEC_HOPS,
    COUNT
};

void OspfInterfaceSyncTimers(OSPF::OspfInterface& iface);
void OspfInterfaceSyncNeighbors(OSPF::OspfInterface& iface);
void OspfInterfaceSyncNetworkType(OSPF::OspfInterface& iface);
void OspfInterfaceDemandCircuit(OSPF::OspfInterface& iface);

using OspfInterfaceRegistry = SubRegistry<__uint128_t, OspfInterface, OSPF::OspfInterface,
    AtomicField<bool, false, OspfInterface::BFD>, // TODO
    OptionalAtomicField<uint16_t, OspfInterface::COST>,
    AtomicField<bool, false, OspfInterface::DATABASE_FILTER>,
    OptionalAtomicField<uint16_t, OspfInterface::DEAD_INTERVAL,
        OSPF::OspfInterface, OspfInterfaceSyncTimers>,
    AtomicField<bool, false, OspfInterface::DEMAND_CIRCUIT,
        OSPF::OspfInterface, OspfInterfaceDemandCircuit>,
    AtomicField<bool, false, OspfInterface::FLOOD_REDUCTION,
        OSPF::OspfInterface, OspfInterfaceDemandCircuit>,
    OptionalAtomicField<uint16_t, OspfInterface::HELLO_INTERVAL,
        OSPF::OspfInterface, OspfInterfaceSyncTimers>,
    OptionalAtomicField<uint8_t, OspfInterface::HELLO_MULTIPLIER,
        OSPF::OspfInterface, OspfInterfaceSyncTimers>,
    AtomicField<bool, false, OspfInterface::MTU_IGNORE>,
    ValueField<std::vector<std::tuple<
        IPAddress,
        std::optional<uint16_t>,
        std::optional<bool>,
        std::optional<uint16_t>,
        std::optional<uint8_t>
    >>, OspfInterface::NEIGHBOR,
        OSPF::OspfInterface, OspfInterfaceSyncNeighbors>,
    AtomicField<OSPF::NetworkType, OSPF::NetworkType::BROADCAST, OspfInterface::NETWORK,
        OSPF::OspfInterface, OspfInterfaceSyncNetworkType>,
    AtomicField<uint8_t, 1, OspfInterface::PRIORITY>,
    AtomicField<bool, false, OspfInterface::PASSIVE>,
    AtomicField<uint16_t, 5, OspfInterface::RETRANSMIT_INTERVAL>,
    AtomicField<uint16_t, 1, OspfInterface::TRANSMIT_DELAY>,
    OptionalAtomicField<bool, OspfInterface::TTL_SEC>, // XXX:
    AtomicField<uint8_t, 1, OspfInterface::TTL_SEC_HOPS> // XXX:
>;

enum class OspfInterfaceAddressFamily : uint8_t
{
    BASE,
    IPV4,
    IPV6,
    COUNT,
};

using OspfInterfaceAddressFamilyRegistry = SimpleSubRegistry<__uint128_t, OspfInterfaceAddressFamily,
    ReferenceContainer<OspfInterfaceRegistry, OspfInterfaceAddressFamily::BASE>,
    ReferenceContainer<OspfInterfaceRegistry, OspfInterfaceAddressFamily::IPV4>,
    ReferenceContainer<OspfInterfaceRegistry, OspfInterfaceAddressFamily::IPV6>
>;

enum class OspfInterfaceIPSec : uint8_t
{
    SPI,
    AUTHENTICATION_TYPE,
    AUTHENTICATION_KEY,
    ENCRYPTION_TYPE,
    ENCRYPTION_KEY,
    COUNT
};

using OspfInterfaceIPSecRegistry  = SubRegistry<uint64_t, OspfInterfaceIPSec, OSPF::OspfInterface,
    OptionalAtomicField<uint32_t, OspfInterfaceIPSec::SPI>, // TODO:
    OptionalAtomicField<OSPF::IPsecAuthType, OspfInterfaceIPSec::AUTHENTICATION_TYPE>, // TODO:
    ValueField<std::array<uint8_t, 40>, OspfInterfaceIPSec::AUTHENTICATION_KEY>, // TODO:
    OptionalAtomicField<OSPF::IPsecEncryptType, OspfInterfaceIPSec::ENCRYPTION_TYPE>, // TODO:
    ValueField<std::array<uint8_t, 64>, OspfInterfaceIPSec::ENCRYPTION_KEY> // TODO:
>;

enum class OspfInterfaceBase : uint8_t
{
    BASE,
    PROCESS_CONFIGS,
    INSTANCE_ID,
    PROCESS_ID,
    AREA_ID,
    INCLUDE_SECONDARIES,
    AUTHENTICATION_TYPE,
    AUTHENTICATION_KEY,
    IPSEC,
    LLS,
    MESSAGE_DIGEST_KEYS,
    MESSAGE_DIGEST_ENCRYPT,
    PREFIX_SUPPRESSION,
    RESYNC_TIMEOUT,
    SHUTDOWN,
    COUNT
};

void OspfInterfaceBaseUpdateDigestKey(OSPF::OspfInterface& iface);
void OspfInterfaceBasePrefixSuppression(OSPF::OspfInterface& iface);

using OspfInterfaceBaseRegistry = SubRegistry<uint64_t, OspfInterfaceBase, OSPF::OspfInterface,
    ReferenceContainer<OspfInterfaceRegistry, OspfInterfaceBase::BASE>,
    OwnedListField<OspfInterfaceAddressFamilyRegistry, uint32_t, OspfInterfaceBase::PROCESS_CONFIGS>,
    AtomicField<uint8_t, 0, OspfInterfaceBase::INSTANCE_ID>,
    OptionalAtomicField<uint16_t, OspfInterfaceBase::PROCESS_ID>,
    OptionalAtomicField<uint32_t, OspfInterfaceBase::AREA_ID>,
    AtomicField<bool, true, OspfInterfaceBase::INCLUDE_SECONDARIES>,
    OptionalAtomicField<OSPF::AuthType, OspfInterfaceBase::AUTHENTICATION_TYPE>,
    OptionalAtomicField<uint64_t, OspfInterfaceBase::AUTHENTICATION_KEY>,
    ReferenceContainer<OspfInterfaceIPSecRegistry, OspfInterfaceBase::IPSEC>,
    OptionalAtomicField<bool, OspfInterfaceBase::LLS>,
    ValueField<std::vector<std::tuple<uint8_t, std::array<uint8_t, 16>, uint64_t>>, OspfInterfaceBase::MESSAGE_DIGEST_KEYS,
        OSPF::OspfInterface, OspfInterfaceBaseUpdateDigestKey>,
    AtomicField<bool, false, OspfInterfaceBase::MESSAGE_DIGEST_ENCRYPT>,
    AtomicField<bool, false, OspfInterfaceBase::PREFIX_SUPPRESSION,
        OSPF::OspfInterface, OspfInterfaceBasePrefixSuppression>,
    AtomicField<uint16_t, 5, OspfInterfaceBase::RESYNC_TIMEOUT>, // TODO 
    AtomicField<bool, false, OspfInterfaceBase::SHUTDOWN> // TODO
>;
}

#endif // OSPF_INTERFACE_REGISTRY_H
