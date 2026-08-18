/**
 * @file Egress.h
 * @brief Factory function for creating the appropriate egress backend.
 * @ingroup HARDWARE_EGRESS
 */

#ifndef EGRESS_H
#define EGRESS_H

#include "EgressBase.h"

namespace qos::egress { struct TxQueueOpts; }

namespace hardware::egress
{
class EgressPacket;

/**
 * @brief Creates the best available egress backend for @p iface.
 *
 * Probes the interface for TPACKET_V2 mmap ring support and returns an
 * @ref EgressPacket if available, falling back to @ref EgressSend (plain
 * sendto) otherwise.
 *
 * @param iface  Interface to transmit on; must remain valid for the lifetime
 *               of the returned object.
 * @param opts   Queue configuration forwarded to the backend constructor.
 * @return Heap-allocated @ref EgressBase subclass. The caller takes ownership.
 *
 * @warning Returns @c nullptr if @p iface is @c nullptr.
 */
EgressBase* create(interface::Interface* iface, const qos::egress::TxQueueOpts& opts);
} // namespace hardware::egress

#endif

