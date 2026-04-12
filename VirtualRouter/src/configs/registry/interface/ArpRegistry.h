/**
 * @file ArpRegistry.h
 * @brief Arp configuration registry
 *
 * Defines the configuration schema for ARP including...
 */

#ifndef ARP_REGISTRY
#define ARP_REGISTRY

#include "configs/RegistryTypes.hpp"
#include "configs/SubRegistry.hpp"

namespace config
{
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

struct ArpRegistry
{
    SubRegistry<Arp,
        AtomicField<bool CONFIG_INDEX_ARG(Arp::AUTHORIZED)>,
        AtomicField<uint32_t CONFIG_INDEX_ARG(Arp::LOG_THRESHOLD_ENTRIES)>,
        AtomicField<bool CONFIG_INDEX_ARG(Arp::PACKET_PRIORITY)>,
        AtomicField<uint8_t CONFIG_INDEX_ARG(Arp::PROBE_INTERVAL)>,
        AtomicField<uint8_t CONFIG_INDEX_ARG(Arp::PROBE_COUNT)>,
        AtomicField<uint32_t CONFIG_INDEX_ARG(Arp::TIMEOUT)>
    > reg;
};
}

#endif // ARP_REGISTRY
