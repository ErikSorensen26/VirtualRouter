/**
 * @file EigrpInterfaceRegistry.h
 * @brief EIGRP interface-specific configuration registry.
 *
 * Defines per-interface EIGRP parameters including metric components
 * (bandwidth, delay, reliability, load, MTU), timers (hello, hold),
 * and interface-level options (passive, summarization).
 */

#ifndef EIGRP_INTERFACE_REGISTRY_H
#define EIGRP_INTERFACE_REGISTRY_H

#include <string>
#include <IPAddress.h>

#include "configs/RegistryBuilder.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/EnumSchema.hpp"

namespace config
{
namespace eigrp
{
#define EIGRP_AUTH_TYPES(X) \
    X(NONE, 0x0000) \
    X(MD5, 0x0002) \
    X(SHA256, 0x0003)

DEFINE_CONFIG_VALUE_ENUM_NS(eigrp, AuthType, EIGRP_AUTH_TYPES, uint16_t);
}

#define EIGRP_SUMMARY_ADDRESS_FIELDS(X) \
    X(types::IPPrefix,                prefix) \
    X(std::optional<std::string>,     leakMap)

DEFINE_TUPLE_SCHEMA(EigrpSummaryAddress, EIGRP_SUMMARY_ADDRESS_FIELDS);

#define EIGRP_INTERFACE_FIELD_LIST(X, Y) \
    OPTIONAL_ATOMIC_FIELD(X, Y, ADD_PATHS, uint8_t) \
    VALUE_FIELD(X, Y, AUTHENTICATION_KEYCHAIN, std::string) \
    ATOMIC_FIELD(X, Y, AUTHENTICATION_MODE, config::eigrp::AuthType, config::eigrp::AuthType::NONE) \
    ATOMIC_FIELD(X, Y, BANDWIDTH_PERCENTAGE, uint32_t, 50) \
    ATOMIC_FIELD(X, Y, BFD, bool, false) \
    ATOMIC_FIELD(X, Y, DAMPENING_CHANGE, bool, false) \
    ATOMIC_FIELD(X, Y, DAMPENING_CHANGE_PERCENT, uint8_t, 50) \
    ATOMIC_FIELD(X, Y, DAMPENING_INTERVAL, bool, false) \
    ATOMIC_FIELD(X, Y, DAMPENING_INTERVAL_TIME, uint16_t, 30) \
    ATOMIC_FIELD(X, Y, HELLO_INTERVAL, uint16_t, 5) \
    ATOMIC_FIELD(X, Y, HOLD_TIME, uint16_t, 15) \
    ATOMIC_FIELD(X, Y, NEXT_HOP_SELF, bool, false) \
    ATOMIC_FIELD_CB(X, Y, PASSIVE_INTERFACE, bool, false) \
    ATOMIC_FIELD_CB(X, Y, SHUTDOWN, bool, false) \
    ATOMIC_FIELD(X, Y, SPLIT_HORIZON, bool, true) \
    LIST_FIELD_CB(X, Y, SUMMARY_ADDRESS, EigrpSummaryAddress)

DEFINE_CONFIG_GROUP(EigrpInterface, EIGRP_INTERFACE_FIELD_LIST)

}

#endif // EIGRP_INTERFACE_REGISTRY_H
