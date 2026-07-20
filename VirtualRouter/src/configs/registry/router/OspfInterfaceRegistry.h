/**
 * @file OspfInterfaceRegistry.h
 * @brief OSPF interface-specific configuration registry.
 *
 * Defines per-interface OSPF parameters including area assignment,
 * router timers (hello, dead), cost metric, network type, authentication,
 * priority (for DR election), and interface options.
 */

#ifndef OSPF_INTERFACE_REGISTRY_H
#define OSPF_INTERFACE_REGISTRY_H

#include <optional>

#include "configs/RegistryTypes.hpp"
#include "IPAddress.h"
#include "configs/SubRegistry.hpp"
#include "configs/RegistryReference.hpp"

#include "configs/FieldAccessor.hpp" // IWYU pragma: keep

namespace config
{
namespace ospf
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

enum class OspfInterfaceBase : uint8_t
{
    DEAD_INTERVAL,
    HELLO_INTERVAL,
    HELLO_MULTIPLIER,
    RETRANSMIT_INTERVAL,
    TRANSMIT_DELAY,
    TTL_SEC,
    TTL_SEC_HOPS,
    COUNT
};

#define OSPF_INTERFACE_BASE_DEFAULTS(X) \
    X(OspfInterfaceBase, RETRANSMIT_INTERVAL, 5) \
    X(OspfInterfaceBase, TRANSMIT_DELAY, 1) \
    X(OspfInterfaceBase, TTL_SEC_HOPS, 1)

CONFIG_DEFAULT_TABLE(OSPF_INTERFACE_BASE_DEFAULTS)

void OspfInterfaceBaseSyncTimers(void* iface);

struct OspfInterfaceBaseFields : FieldTuple<
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterfaceBase::DEAD_INTERVAL),
        OspfInterfaceBaseSyncTimers>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterfaceBase::HELLO_INTERVAL),
        OspfInterfaceBaseSyncTimers>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(OspfInterfaceBase::HELLO_MULTIPLIER),
        OspfInterfaceBaseSyncTimers>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterfaceBase::RETRANSMIT_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterfaceBase::TRANSMIT_DELAY)>,
    OptionalAtomicField<bool CONFIG_INDEX_ARG(OspfInterfaceBase::TTL_SEC)>, // XXX:
    AtomicField<uint8_t CONFIG_INDEX_ARG(OspfInterfaceBase::TTL_SEC_HOPS)> // XXX:
> {};

struct OspfInterfaceBaseRegistry : SubRegistry<OspfInterfaceBaseRegistry, OspfInterfaceBase, nullptr, OspfInterfaceBaseFields> {};

enum class OspfInterface : uint8_t
{
    BASE,
    BFD,
    COST,
    DATABASE_FILTER,
    DEMAND_CIRCUIT,
    DEMAND_CIRCUIT_IGNORE,
    FLOOD_REDUCTION,
    GRACEFUL_RESTART_HELPER,
    MTU_IGNORE,
    NEIGHBOR,
    NETWORK,
    PRIORITY,
    PASSIVE,
    COUNT
};

#define OSPF_INTERFACE_DEFAULTS(X) \
    X(OspfInterface, BFD, false) \
    X(OspfInterface, DATABASE_FILTER, false) \
    X(OspfInterface, DEMAND_CIRCUIT, false) \
    X(OspfInterface, DEMAND_CIRCUIT_IGNORE, false) \
    X(OspfInterface, FLOOD_REDUCTION, false) \
    X(OspfInterface, GRACEFUL_RESTART_HELPER, true) \
    X(OspfInterface, MTU_IGNORE, false) \
    X(OspfInterface, NETWORK, ospf::NetworkType::BROADCAST) \
    X(OspfInterface, PRIORITY, 1) \
    X(OspfInterface, PASSIVE, false) \

CONFIG_DEFAULT_TABLE(OSPF_INTERFACE_DEFAULTS)

void OspfInterfaceSyncNeighbors(void* iface);
void OspfInterfaceSyncNetworkType(void* iface);
void OspfInterfaceDemandCircuit(void* iface);
void OspfInterfaceSyncPassive(void* iface);

struct OspfInterfaceFields : FieldTuple<
    RegistryContainer<OspfInterfaceBaseRegistry CONFIG_INDEX_ARG(OspfInterface::BASE)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::BFD)>, // TODO
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(OspfInterface::COST)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::DATABASE_FILTER)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::DEMAND_CIRCUIT),
        OspfInterfaceDemandCircuit>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::DEMAND_CIRCUIT_IGNORE),
        OspfInterfaceDemandCircuit>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::FLOOD_REDUCTION),
        OspfInterfaceDemandCircuit>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::GRACEFUL_RESTART_HELPER)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::MTU_IGNORE)>,
    ListField<std::tuple<
        types::IPAddress,           // neighbor address
        IGNOR(std::optional<uint16_t>),    // cost
        IGNOR(bool),                       // database-filter
        IGNOR(std::optional<uint16_t>),    // poll interval
        IGNOR(std::optional<uint8_t>)
    > CONFIG_INDEX_ARG(OspfInterface::NEIGHBOR),
        OspfInterfaceSyncNeighbors>,
    AtomicField<ospf::NetworkType CONFIG_INDEX_ARG(OspfInterface::NETWORK),
        OspfInterfaceSyncNetworkType>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(OspfInterface::PRIORITY)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfInterface::PASSIVE),
        OspfInterfaceSyncPassive>
> {};

struct OspfInterfaceRegistry : SubRegistry<OspfInterfaceRegistry, OspfInterface, nullptr, OspfInterfaceFields> {};

enum class OspfInterfaceAddressFamily : uint8_t
{
    BASE,
    IPV4,
    IPV6,
    COUNT,
};

struct OspfInterfaceAddressFamilyFields : FieldTuple<
    RegistryContainer<OspfInterfaceRegistry CONFIG_INDEX_ARG(OspfInterfaceAddressFamily::BASE)>,
    RegistryContainer<OspfInterfaceRegistry CONFIG_INDEX_ARG(OspfInterfaceAddressFamily::IPV4)>,
    RegistryContainer<OspfInterfaceRegistry CONFIG_INDEX_ARG(OspfInterfaceAddressFamily::IPV6)>
> {};

struct OspfInterfaceAddressFamilyRegistry : SubRegistry<OspfInterfaceAddressFamilyRegistry, OspfInterfaceAddressFamily, nullptr, OspfInterfaceAddressFamilyFields> {};

enum class OspfInterfaceIPSec : uint8_t
{
    SPI,
    AUTHENTICATION_TYPE,
    AUTHENTICATION_KEY,
    ENCRYPTION_TYPE,
    ENCRYPTION_KEY,
    COUNT
};

struct OspfInterfaceIPSecFields : FieldTuple<
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(OspfInterfaceIPSec::SPI)>, // TODO:
    OptionalAtomicField<ospf::IPsecAuthType CONFIG_INDEX_ARG(OspfInterfaceIPSec::AUTHENTICATION_TYPE)>, // TODO:
    ValueField<std::array<uint8_t, 40> CONFIG_INDEX_ARG(OspfInterfaceIPSec::AUTHENTICATION_KEY)>, // TODO:
    OptionalAtomicField<ospf::IPsecEncryptType CONFIG_INDEX_ARG(OspfInterfaceIPSec::ENCRYPTION_TYPE)>, // TODO:
    ValueField<std::array<uint8_t, 64> CONFIG_INDEX_ARG(OspfInterfaceIPSec::ENCRYPTION_KEY)> // TODO:
> {};

struct OspfInterfaceIPSecRegistry : SubRegistry<OspfInterfaceIPSecRegistry, OspfInterfaceIPSec, nullptr, OspfInterfaceIPSecFields> {};

enum class OspfGlobalInterfaceBase : uint8_t
{
    INSTANCE_ID,
    AUTHENTICATION_TYPE,
    AUTHENTICATION_KEY,
    MESSAGE_DIGEST_KEYS,
    MESSAGE_DIGEST_ENCRYPT,
    COUNT
};

#define OSPF_GLOBAL_INTERFACE_BASE_DEFAULTS(X) \
    X(OspfGlobalInterfaceBase, INSTANCE_ID, 0) \
    X(OspfGlobalInterfaceBase, MESSAGE_DIGEST_ENCRYPT, false)

CONFIG_DEFAULT_TABLE(OSPF_GLOBAL_INTERFACE_BASE_DEFAULTS)

void OspfGlobalInterfaceBaseUpdateDigestKey(void* iface);

struct OspfGlobalInterfaceBaseFields : FieldTuple<
    AtomicField<uint8_t CONFIG_INDEX_ARG(OspfGlobalInterfaceBase::INSTANCE_ID)>,
    OptionalAtomicField<ospf::AuthType CONFIG_INDEX_ARG(OspfGlobalInterfaceBase::AUTHENTICATION_TYPE)>,
    OptionalAtomicField<uint64_t CONFIG_INDEX_ARG(OspfGlobalInterfaceBase::AUTHENTICATION_KEY)>,
    ListField<std::tuple<uint8_t, IgnoreCompare<std::array<uint8_t, 16>>> CONFIG_INDEX_ARG(OspfGlobalInterfaceBase::MESSAGE_DIGEST_KEYS),
        OspfGlobalInterfaceBaseUpdateDigestKey>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfGlobalInterfaceBase::MESSAGE_DIGEST_ENCRYPT)>
> {};

struct OspfGlobalInterfaceBaseRegistry : SubRegistry<OspfGlobalInterfaceBaseRegistry, OspfGlobalInterfaceBase, nullptr, OspfGlobalInterfaceBaseFields> {};

enum class OspfGlobalInterface : uint8_t
{
    BASE,
    GLOBAL_BASE,
    PROCESS_CONFIGS,
    PROCESS_ID,
    AREA_ID,
    INCLUDE_SECONDARIES,
    IPSEC,
    LLS,
    PREFIX_SUPPRESSION,
    RESYNC_TIMEOUT,
    SHUTDOWN,
    COUNT
};

#define OSPF_GLOBAL_INTERFACE_DEFAULTS(X) \
    X(OspfGlobalInterface, INCLUDE_SECONDARIES, true) \
    X(OspfGlobalInterface, PREFIX_SUPPRESSION, false) \
    X(OspfGlobalInterface, RESYNC_TIMEOUT, 5) \
    X(OspfGlobalInterface, SHUTDOWN, false)

CONFIG_DEFAULT_TABLE(OSPF_GLOBAL_INTERFACE_DEFAULTS)

void OspfGlobalInterfacePrefixSuppression(void* iface);

struct OspfGlobalInterfaceFields : FieldTuple<
    RegistryContainer<OspfInterfaceRegistry CONFIG_INDEX_ARG(OspfGlobalInterface::BASE)>,
    RegistryContainer<OspfGlobalInterfaceBaseRegistry CONFIG_INDEX_ARG(OspfGlobalInterface::GLOBAL_BASE)>,
    OwnedListField<OspfInterfaceAddressFamilyRegistry, uint32_t CONFIG_INDEX_ARG(OspfGlobalInterface::PROCESS_CONFIGS)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(OspfGlobalInterface::PROCESS_ID)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(OspfGlobalInterface::AREA_ID)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfGlobalInterface::INCLUDE_SECONDARIES)>,
    RegistryContainer<OspfInterfaceIPSecRegistry CONFIG_INDEX_ARG(OspfGlobalInterface::IPSEC)>,
    OptionalAtomicField<bool CONFIG_INDEX_ARG(OspfGlobalInterface::LLS)>,
    AtomicField<bool CONFIG_INDEX_ARG(OspfGlobalInterface::PREFIX_SUPPRESSION),
        OspfGlobalInterfacePrefixSuppression>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(OspfGlobalInterface::RESYNC_TIMEOUT)>, // TODO 
    AtomicField<bool CONFIG_INDEX_ARG(OspfGlobalInterface::SHUTDOWN)> // TODO
> {};

struct OspfGlobalInterfaceRegistry : SubRegistry<OspfGlobalInterfaceRegistry, OspfGlobalInterface, nullptr, OspfGlobalInterfaceFields> {};

enum class OspfInterfaceAf
{
    IPV4,
    IPV6,
    DEFAULT,
    COUNT
};

struct OspfInterfaceAfFields : FieldTuple<
    RegistryContainer<OspfGlobalInterfaceRegistry CONFIG_INDEX_ARG(OspfInterfaceAf::IPV4)>,
    RegistryContainer<OspfGlobalInterfaceRegistry CONFIG_INDEX_ARG(OspfInterfaceAf::IPV6)>,
    RegistryContainer<OspfGlobalInterfaceRegistry CONFIG_INDEX_ARG(OspfInterfaceAf::DEFAULT)>
> {};

struct OspfInterfaceAfRegistry : SubRegistry<OspfInterfaceAfRegistry, OspfInterfaceAf, nullptr, OspfInterfaceAfFields> {};

enum class OspfVirtualLink
{
    GLOBAL_BASE,
    BASE,
    COUNT
};

struct OspfVirtualLinkFields : FieldTuple<
    RegistryContainer<OspfGlobalInterfaceBaseRegistry CONFIG_INDEX_ARG(OspfVirtualLink::GLOBAL_BASE)>,
    RegistryContainer<OspfInterfaceBaseRegistry CONFIG_INDEX_ARG(OspfVirtualLink::BASE)>
> {};

struct OspfVirtualLinkRegistry : public SubRegistry<OspfVirtualLinkRegistry, OspfVirtualLink, nullptr, OspfVirtualLinkFields> {};
}

#endif // OSPF_INTERFACE_REGISTRY_H
