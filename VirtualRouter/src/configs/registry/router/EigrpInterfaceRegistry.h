// EigrpInterfaceRegistry.h

#ifndef EIGRP_INTERFACE_REGISTRY_H
#define EIGRP_INTERFACE_REGISTRY_H

#include <string>
#include <IPAddress.h>

#include "configs/RegistryTypes.hpp"
#include "configs/RegistryDefaultTable.hpp"
#include "configs/SubRegistry.hpp"

 // namespace eigrp

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

enum class EigrpInterface
{
    AUTHENTICATION_KEYCHAIN,
    AUTHENTICATION_MODE,
    BANDWIDTH_PERCENTAGE,
    BFD,
    DAMPENING_CHANGE,
    DAMPENING_CHANGE_PERCENT,
    DAMPENING_INTERVAL,
    DAMPENING_INTERVAL_TIME,
    HELLO_INTERVAL,
    HOLD_TIME,
    NEXT_HOP_SELF,
    PASSIVE_INTERFACE,
    SHUTDOWN,
    SPLIT_HORIZON,
    SUMMARY_ADDRESS,
    COUNT
};

#define EIGRP_INTERFACE_DEFAULTS(X) \
    X(EigrpInterface, AUTHENTICATION_MODE,      config::eigrp::AuthType::NONE) \
    X(EigrpInterface, BANDWIDTH_PERCENTAGE,     50) \
    X(EigrpInterface, BFD,                      false) \
    X(EigrpInterface, DAMPENING_CHANGE,         false) \
    X(EigrpInterface, DAMPENING_CHANGE_PERCENT, 1) \
    X(EigrpInterface, DAMPENING_INTERVAL,       false) \
    X(EigrpInterface, DAMPENING_INTERVAL_TIME,  5) \
    X(EigrpInterface, HELLO_INTERVAL,           5) \
    X(EigrpInterface, HOLD_TIME,                15) \
    X(EigrpInterface, NEXT_HOP_SELF,            false) \
    X(EigrpInterface, PASSIVE_INTERFACE,        false) \
    X(EigrpInterface, SHUTDOWN,                 false) \
    X(EigrpInterface, SPLIT_HORIZON,            true)

CONFIG_DEFAULT_TABLE(EIGRP_INTERFACE_DEFAULTS);

void EigrpIfacePassive(void* i);
void EigrpIfaceShutdown(void* i);
void EigrpIfaceSummary(void* i);

using EigrpInterfaceRegistry = SubRegistry<EigrpInterface,
    OptionalValueField<std::string CONFIG_INDEX_ARG(EigrpInterface::AUTHENTICATION_KEYCHAIN)>,
    AtomicField<config::eigrp::AuthType CONFIG_INDEX_ARG(EigrpInterface::AUTHENTICATION_MODE)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(EigrpInterface::BANDWIDTH_PERCENTAGE)>,
    AtomicField<bool CONFIG_INDEX_ARG(EigrpInterface::BFD)>,
    AtomicField<bool CONFIG_INDEX_ARG(EigrpInterface::DAMPENING_CHANGE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(EigrpInterface::DAMPENING_CHANGE_PERCENT)>,
    AtomicField<bool CONFIG_INDEX_ARG(EigrpInterface::DAMPENING_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(EigrpInterface::DAMPENING_INTERVAL_TIME)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(EigrpInterface::HELLO_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(EigrpInterface::HOLD_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(EigrpInterface::NEXT_HOP_SELF)>,
    AtomicField<bool CONFIG_INDEX_ARG(EigrpInterface::PASSIVE_INTERFACE), EigrpIfacePassive>,
    AtomicField<bool CONFIG_INDEX_ARG(EigrpInterface::SHUTDOWN), EigrpIfaceShutdown>,
    AtomicField<bool CONFIG_INDEX_ARG(EigrpInterface::SPLIT_HORIZON)>,
    ValueField<std::vector<std::tuple<types::IPAddress, uint8_t>> CONFIG_INDEX_ARG(EigrpInterface::SUMMARY_ADDRESS), EigrpIfaceSummary>
>;

}

#endif // EIGRP_INTERFACE_REGISTRY_H
