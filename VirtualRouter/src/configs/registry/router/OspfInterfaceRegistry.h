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
#include "configs/RegistryBuilder.hpp"
#include "configs/EnumSchema.hpp"

#include "configs/FieldAccessor.hpp" // IWYU pragma: keep

namespace config
{
namespace ospf
{
class OspfInterface;

#define OSPF_NETWORK_TYPE(X) \
    X(BROADCAST) \
    X(NON_BROADCAST) \
    X(POINT_TO_MULTIPOINT) \
    X(POINT_TO_MULTIPOINT_BROADCAST) \
    X(POINT_TO_POINT)

DEFINE_CONFIG_ENUM_NS(ospf, NetworkType, OSPF_NETWORK_TYPE);

#define OSPF_AUTH_TYPE(X) \
    X(NULL_AUTH, 0) \
    X(SIMPLE, 1) \
    X(CRYPTO, 2)

DEFINE_CONFIG_VALUE_ENUM_NS(ospf, AuthType, OSPF_AUTH_TYPE, uint8_t);

#define OSPF_IPSEC_AUTH_TYPE(X) \
    X(NULL_AUTH) \
    X(MD5) \
    X(SHA1)

DEFINE_CONFIG_ENUM_NS(ospf, IPsecAuthType, OSPF_IPSEC_AUTH_TYPE);

#define OSPF_IPSEC_ENCRYPT_TYPE(X) \
    X(_3DES) \
    X(AES_CBC_128) \
    X(AES_CBC_192) \
    X(AES_CBC_256) \
    X(DES) \
    X(NULL_TYPE)

DEFINE_CONFIG_ENUM_NS(ospf, IPsecEncryptType, OSPF_IPSEC_ENCRYPT_TYPE);
}

void OspfInterfaceBaseSyncTimers(void* iface);

#define OSPF_INTERFACE_BASE_FIELD_LIST(X, Y) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, DEAD_INTERVAL, uint16_t, OspfInterfaceBaseSyncTimers) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, HELLO_INTERVAL, uint16_t, OspfInterfaceBaseSyncTimers) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, HELLO_MULTIPLIER, uint8_t, OspfInterfaceBaseSyncTimers) \
    ATOMIC_FIELD(X, Y, RETRANSMIT_INTERVAL, uint16_t, 5) \
    ATOMIC_FIELD(X, Y, TRANSMIT_DELAY, uint16_t, 1) \
    OPTIONAL_ATOMIC_FIELD(X, Y, TTL_SEC, bool) TODO \
    ATOMIC_FIELD(X, Y, TTL_SEC_HOPS, uint8_t, 1) TODO

DEFINE_CONFIG_GROUP(OspfInterfaceBase, OSPF_INTERFACE_BASE_FIELD_LIST);

void OspfInterfaceSyncNeighbors(void* iface);
void OspfInterfaceSyncNetworkType(void* iface);
void OspfInterfaceDemandCircuit(void* iface);
void OspfInterfaceSyncPassive(void* iface);

#define OSPF_NEIGHBOR_FIELDS(X) \
    X(types::IPAddress,               address) \
    X(IGNOR(std::optional<uint16_t>), cost) \
    X(IGNOR(bool),                    databaseFilter) \
    X(IGNOR(std::optional<uint16_t>), pollInterval) \
    X(IGNOR(std::optional<uint8_t>),  priority)

DEFINE_TUPLE_SCHEMA(OspfNeighbor, OSPF_NEIGHBOR_FIELDS);

#define OSPF_INTERFACE_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, BASE, OspfInterfaceBaseRegistry) \
    ATOMIC_FIELD(X, Y, BFD, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, COST, uint16_t) \
    ATOMIC_FIELD(X, Y, DATABASE_FILTER, bool, false) \
    ATOMIC_FIELD_CB(X, Y, DEMAND_CIRCUIT, bool, false, OspfInterfaceDemandCircuit) \
    ATOMIC_FIELD_CB(X, Y, DEMAND_CIRCUIT_IGNORE, bool, false, OspfInterfaceDemandCircuit) \
    ATOMIC_FIELD_CB(X, Y, FLOOD_REDUCTION, bool, false, OspfInterfaceDemandCircuit) \
    ATOMIC_FIELD(X, Y, GRACEFUL_RESTART_HELPER, bool, true) \
    ATOMIC_FIELD(X, Y, MTU_IGNORE, bool, false) \
    LIST_FIELD(X, Y, NEIGHBOR, OspfNeighbor) \
    ATOMIC_FIELD_CB(X, Y, NETWORK, ospf::NetworkType, ospf::NetworkType::BROADCAST, OspfInterfaceSyncNetworkType) \
    ATOMIC_FIELD(X, Y, PRIORITY, uint8_t, 1) \
    ATOMIC_FIELD_CB(X, Y, PASSIVE, bool, false, OspfInterfaceSyncPassive)

DEFINE_CONFIG_GROUP(OspfInterface, OSPF_INTERFACE_FIELD_LIST);

#define OSPF_INTERFACE_ADDRESS_FAMILY(X, Y) \
    REGISTRY_CONTAINER(X, Y, BASE, OspfInterfaceRegistry) \
    REGISTRY_CONTAINER(X, Y, IPV4, OspfInterfaceRegistry) \
    REGISTRY_CONTAINER(X, Y, IPV6, OspfInterfaceRegistry)

DEFINE_CONFIG_GROUP(OspfInterfaceAddressFamily, OSPF_INTERFACE_ADDRESS_FAMILY)

#define OSPF_IPSEC_FIELD_LIST(X, Y) \
    OPTIONAL_ATOMIC_FIELD(X, Y, SPI, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, AUTHENTICATION_TYPE, ospf::IPsecAuthType) \
    VALUE_FIELD(X, Y, AUTHENTICATION_KEY, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, ENCRYPTION_TYPE, ospf::IPsecEncryptType) \
    VALUE_FIELD(X, Y, ENCRYPTION_KEY, std::string)

DEFINE_CONFIG_GROUP(OspfIPsec, OSPF_IPSEC_FIELD_LIST);

#define OSPF_MESSAGE_DIGEST_KEY(X) \
    X(uint8_t,            keyId) \
    X(IGNOR(std::string), digest)

DEFINE_TUPLE_SCHEMA(OspfMessageDigestKey, OSPF_MESSAGE_DIGEST_KEY);

void OspfGlobalInterfaceBaseUpdateDigestKey(void* iface);

#define OSPF_GLOBAL_INTERFACE_BASE_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, INSTANCE_ID, uint8_t, 0) \
    OPTIONAL_ATOMIC_FIELD(X, Y, AUTHENTICATION_TYPE, ospf::AuthType) \
    OPTIONAL_ATOMIC_FIELD(X, Y, AUTHENTICATION_KEY, uint64_t) \
    LIST_FIELD_CB(X, Y, MESSAGE_DIGEST_KEYS, OspfMessageDigestKey, OspfGlobalInterfaceBaseUpdateDigestKey) \
    ATOMIC_FIELD(X, Y, MESSAGE_DIGEST_ENCRYPT, bool, false)

DEFINE_CONFIG_GROUP(OspfGlobalInterfaceBase, OSPF_GLOBAL_INTERFACE_BASE_FIELD_LIST);

void OspfGlobalInterfacePrefixSuppression(void* iface);

#define OSPF_GLOBAL_INTERFACE_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, BASE, OspfInterfaceRegistry) \
    REGISTRY_CONTAINER(X, Y, GLOBAL_BASE, OspfGlobalInterfaceBaseRegistry) \
    OWNED_LIST_FIELD(X, Y, PROCESS_CONFIGS, OspfInterfaceAddressFamilyRegistry, uint32_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, PROCESS_ID, uint16_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, AREA_ID, uint32_t) \
    ATOMIC_FIELD(X, Y, INCLUDE_SECONDARIES, bool, true) \
    REGISTRY_CONTAINER(X, Y, IPSEC, OspfIPsecRegistry) \
    OPTIONAL_ATOMIC_FIELD(X, Y, LLS, bool) \
    ATOMIC_FIELD_CB(X, Y, PREFIX_SUPPRESSION, bool, false, OspfGlobalInterfacePrefixSuppression) \
    ATOMIC_FIELD(X, Y, RESYNC_TIMEOUT, uint16_t, 5) TODO \
    ATOMIC_FIELD(X, Y, SHUTDOWN, bool, false) TODO

DEFINE_CONFIG_GROUP(OspfGlobalInterface, OSPF_GLOBAL_INTERFACE_FIELD_LIST);

#define OSPF_INTERFACE_AF_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, IPV4, OspfGlobalInterfaceRegistry) \
    REGISTRY_CONTAINER(X, Y, IPV6, OspfGlobalInterfaceRegistry) \
    REGISTRY_CONTAINER(X, Y, DEFAULT, OspfGlobalInterfaceRegistry)

DEFINE_CONFIG_GROUP(OspfInterfaceAf, OSPF_INTERFACE_AF_FIELD_LIST);

#define OSPF_VIRTUAL_LINK_FIELD_LIST(X, Y) \
    REGISTRY_CONTAINER(X, Y, GLOBAL_BASE, OspfGlobalInterfaceBaseRegistry) \
    REGISTRY_CONTAINER(X, Y, BASE, OspfInterfaceBaseRegistry)

DEFINE_CONFIG_GROUP(OspfVirtualLink, OSPF_VIRTUAL_LINK_FIELD_LIST);
}

#endif // OSPF_INTERFACE_REGISTRY_H
