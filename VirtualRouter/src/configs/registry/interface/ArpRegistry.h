/**
 * @file ArpRegistry.h
 * @brief Per-interface ARP configuration schema (timeouts, probing, and authorization).
 * @ingroup CONFIG_INTERFACE
 */

#ifndef ARP_REGISTRY
#define ARP_REGISTRY

#include "configs/RegistryBuilder.hpp"

namespace config
{
#define ARP_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, AUTHORIZED, bool, false) \
    ATOMIC_FIELD(X, Y, LOG_THRESHOLD_ENTRIES, uint32_t, 32) \
    ATOMIC_FIELD(X, Y, PACKET_PRIORITY, bool, true) \
    ATOMIC_FIELD(X, Y, PROBE_INTERVAL, uint8_t, 30) \
    ATOMIC_FIELD(X, Y, PROBE_COUNT, uint8_t, 2) \
    ATOMIC_FIELD(X, Y, TIMEOUT, uint32_t, 14400)

/**
 * @brief Per-interface ARP configuration fields.
 * @ingroup CONFIG_INTERFACE
 */
DEFINE_CONFIG_GROUP(Arp, ARP_FIELD_LIST)
}

#endif // ARP_REGISTRY
