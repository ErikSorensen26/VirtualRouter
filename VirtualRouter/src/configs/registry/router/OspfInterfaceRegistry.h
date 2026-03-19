// OspfInterfaceRegistry.h

#ifndef OSPF_INTERFACE_REGISTRY_H
#define OSPF_INTERFACE_REGISTRY_H

#include <optional>
#include <vector>

#include "configs/RegistryTypes.hpp"

#include "IPAddress.h"
#include "AddressFamily.hpp"
#include "packet/HeaderHelpers.hpp"
#include "configs/SubRegistry.hpp"
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

#define OSPF_INTERFACE_DEFAULTS(X) \
    X(OspfInterface, BFD, false) \
    X(OspfInterface, DATABASE_FILTER, false) \
    X(OspfInterface, DEMAND_CIRCUIT, false) \
    X(OspfInterface, FLOOD_REDUCTION, false) \
    X(OspfInterface, MTU_IGNORE, false) \
    X(OspfInterface, NETWORK, OSPF::NetworkType::BROADCAST) \
    X(OspfInterface, PRIORITY, 1) \
    X(OspfInterface, PASSIVE, false) \
    X(OspfInterface, RETRANSMIT_INTERVAL, 5) \
    X(OspfInterface, TRANSMIT_DELAY, 1) \
    X(OspfInterface, TTL_SEC_HOPS, 1)

CONFIG_DEFAULT_TABLE(OSPF_INTERFACE_DEFAULTS)

void OspfInterfaceSyncTimers(void* iface);
void OspfInterfaceSyncNeighbors(void* iface);
void OspfInterfaceSyncNetworkType(void* iface);
void OspfInterfaceDemandCircuit(void* iface);

using OspfInterfaceRegistry = SubRegistry<OspfInterface,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::BFD)>, // TODO
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterface::COST)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::DATABASE_FILTER)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterface::DEAD_INTERVAL),
        OspfInterfaceSyncTimers>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::DEMAND_CIRCUIT),
        OspfInterfaceDemandCircuit>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::FLOOD_REDUCTION),
        OspfInterfaceDemandCircuit>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterface::HELLO_INTERVAL),
        OspfInterfaceSyncTimers>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(OspfInterface::HELLO_MULTIPLIER),
        OspfInterfaceSyncTimers>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::MTU_IGNORE)>,
    ValueField<std::vector<std::tuple<
        IPAddress,
        std::optional<uint16_t>,
        std::optional<bool>,
        std::optional<uint16_t>,
        std::optional<uint8_t>
    >> CONFIG_INDEX_ARG(OspfInterface::NEIGHBOR),
        OspfInterfaceSyncNeighbors>,
    AtomicField<OSPF::NetworkType CONFIG_INDEX_ARG(OspfInterface::NETWORK),
        OspfInterfaceSyncNetworkType>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(OspfInterface::PRIORITY)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::PASSIVE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterface::RETRANSMIT_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterface::TRANSMIT_DELAY)>,
    OptionalAtomicField<bool CONFIG_INDEX_ARG(OspfInterface::TTL_SEC)>, // XXX:
    AtomicField<uint8_t CONFIG_INDEX_ARG(OspfInterface::TTL_SEC_HOPS)> // XXX:
>;

enum class OspfInterfaceAddressFamily : uint8_t
{
    BASE,
    IPV4,
    IPV6,
    COUNT,
};

using OspfInterfaceAddressFamilyRegistry = SubRegistry<OspfInterfaceAddressFamily,
    ReferenceContainer<OspfInterfaceRegistry CONFIG_INDEX_ARG(OspfInterfaceAddressFamily::BASE)>,
    ReferenceContainer<OspfInterfaceRegistry CONFIG_INDEX_ARG(OspfInterfaceAddressFamily::IPV4)>,
    ReferenceContainer<OspfInterfaceRegistry CONFIG_INDEX_ARG(OspfInterfaceAddressFamily::IPV6)>
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

using OspfInterfaceIPSecRegistry = SubRegistry<OspfInterfaceIPSec,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(OspfInterfaceIPSec::SPI)>, // TODO:
    OptionalAtomicField<OSPF::IPsecAuthType CONFIG_INDEX_ARG(OspfInterfaceIPSec::AUTHENTICATION_TYPE)>, // TODO:
    ValueField<std::array<uint8_t, 40> CONFIG_INDEX_ARG(OspfInterfaceIPSec::AUTHENTICATION_KEY)>, // TODO:
    OptionalAtomicField<OSPF::IPsecEncryptType CONFIG_INDEX_ARG(OspfInterfaceIPSec::ENCRYPTION_TYPE)>, // TODO:
    ValueField<std::array<uint8_t, 64> CONFIG_INDEX_ARG(OspfInterfaceIPSec::ENCRYPTION_KEY)> // TODO:
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

#define OSPF_INTERFACE_BASE_DEFAULTS(X) \
    X(OspfInterfaceBase, INSTANCE_ID, 0) \
    X(OspfInterfaceBase, INCLUDE_SECONDARIES, true) \
    X(OspfInterfaceBase, MESSAGE_DIGEST_ENCRYPT, false) \
    X(OspfInterfaceBase, PREFIX_SUPPRESSION, false) \
    X(OspfInterfaceBase, RESYNC_TIMEOUT, 5) \
    X(OspfInterfaceBase, SHUTDOWN, false)

CONFIG_DEFAULT_TABLE(OSPF_INTERFACE_BASE_DEFAULTS)

void OspfInterfaceBaseUpdateDigestKey(void* iface);
void OspfInterfaceBasePrefixSuppression(void* iface);

using OspfInterfaceBaseRegistry = SubRegistry<OspfInterfaceBase,
    ReferenceContainer<OspfInterfaceRegistry CONFIG_INDEX_ARG(OspfInterfaceBase::BASE)>,
    OwnedListField<OspfInterfaceAddressFamilyRegistry, uint32_t CONFIG_INDEX_ARG(OspfInterfaceBase::PROCESS_CONFIGS)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(OspfInterfaceBase::INSTANCE_ID)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterfaceBase::PROCESS_ID)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(OspfInterfaceBase::AREA_ID)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterfaceBase::INCLUDE_SECONDARIES)>,
    OptionalAtomicField<OSPF::AuthType CONFIG_INDEX_ARG(OspfInterfaceBase::AUTHENTICATION_TYPE)>,
    OptionalAtomicField<uint64_t CONFIG_INDEX_ARG(OspfInterfaceBase::AUTHENTICATION_KEY)>,
    ReferenceContainer<OspfInterfaceIPSecRegistry CONFIG_INDEX_ARG(OspfInterfaceBase::IPSEC)>,
    OptionalAtomicField<bool CONFIG_INDEX_ARG(OspfInterfaceBase::LLS)>,
    ValueField<std::vector<std::tuple<uint8_t, std::array<uint8_t, 16>, uint64_t>> CONFIG_INDEX_ARG(OspfInterfaceBase::MESSAGE_DIGEST_KEYS),
        OspfInterfaceBaseUpdateDigestKey>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterfaceBase::MESSAGE_DIGEST_ENCRYPT)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterfaceBase::PREFIX_SUPPRESSION),
        OspfInterfaceBasePrefixSuppression>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterfaceBase::RESYNC_TIMEOUT)>, // TODO 
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterfaceBase::SHUTDOWN)> // TODO
>;
}

#endif // OSPF_INTERFACE_REGISTRY_H
