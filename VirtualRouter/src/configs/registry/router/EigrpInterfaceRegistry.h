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

#include "configs/RegistryTypes.hpp"
#include "configs/RegistryBuilder.hpp"
#include "configs/RegistryReference.hpp"
#include "configs/TupleSchema.hpp"

namespace config
{
namespace eigrp
{
enum class AuthType : uint16_t
{
    NONE = 0x0000,
    MD5 = 0x0002,
    SHA256 = 0x0003
};
}

void EigrpIfacePassive(void* i);
void EigrpIfaceShutdown(void* i);
void EigrpIfaceSummary(void* i);

#define EIGRP_SUMMARY_ADDRESS_FIELDS(X) \
    X(types::IPPrefix,                prefix) \
    X(std::optional<std::string>,     leakMap)

DEFINE_TUPLE_SCHEMA(EigrpSummaryAddress, EIGRP_SUMMARY_ADDRESS_FIELDS);

// AUTHENTICATION_KEYCHAIN and SUMMARY_ADDRESS carried no default row: absence is
// what a ValueField and a ListField already store before configuration.
#define EIGRP_INTERFACE_FIELD_LIST(X, Y) \
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
    ATOMIC_FIELD_CB(X, Y, PASSIVE_INTERFACE, bool, false, EigrpIfacePassive) \
    ATOMIC_FIELD_CB(X, Y, SHUTDOWN, bool, false, EigrpIfaceShutdown) \
    ATOMIC_FIELD(X, Y, SPLIT_HORIZON, bool, true) \
    LIST_FIELD_CB(X, Y, SUMMARY_ADDRESS, EigrpSummaryAddress, EigrpIfaceSummary)

DEFINE_CONFIG_GROUP(EigrpInterface, EIGRP_INTERFACE_FIELD_LIST)
}

#endif // EIGRP_INTERFACE_REGISTRY_H
