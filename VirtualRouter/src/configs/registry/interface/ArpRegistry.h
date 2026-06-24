/**
 * @file ArpRegistry.h
 * @brief Per-interface ARP configuration schema (timeouts, probing, and authorization).
 * @ingroup CONFIG_INTERFACE
 */

#ifndef ARP_REGISTRY
#define ARP_REGISTRY

#include "configs/RegistryTypes.hpp"
#include "configs/SubRegistry.hpp"

namespace config
{
/**
 * @brief Per-interface ARP configuration fields.
 * @ingroup CONFIG_INTERFACE
 */
enum class Arp
{
    AUTHORIZED,
    LOG_THRESHOLD_ENTRIES,
    PACKET_PRIORITY,
    PROBE_INTERVAL,
    PROBE_COUNT,
    TIMEOUT,
    COUNT
};

#define ARP_DEFAULTS(X) \
    X(Arp, AUTHORIZED, false) \
    X(Arp, LOG_THRESHOLD_ENTRIES, 32) \
    X(Arp, PACKET_PRIORITY, true) \
    X(Arp, PROBE_INTERVAL, 30) \
    X(Arp, PROBE_COUNT, 2) \
    X(Arp, TIMEOUT, 14400)

CONFIG_DEFAULT_TABLE(ARP_DEFAULTS);

struct ArpFields : FieldTuple<
    AtomicField<bool CONFIG_INDEX_ARG(Arp::AUTHORIZED)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Arp::LOG_THRESHOLD_ENTRIES)>,
    AtomicField<bool CONFIG_INDEX_ARG(Arp::PACKET_PRIORITY)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Arp::PROBE_INTERVAL)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Arp::PROBE_COUNT)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Arp::TIMEOUT)>
> {};

struct ArpRegistry : SubRegistry<ArpRegistry, Arp, nullptr, ArpFields> {};
}

#endif // ARP_REGISTRY
