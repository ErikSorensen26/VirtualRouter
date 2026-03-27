/**
 * @file Ingress.h
 * @brief Factory function for creating the appropriate ingress backend.
 */

#ifndef INGRESS_H
#define INGRESS_H

#include "IngressBase.h"

namespace hardware::ingress
{
class IngressXdp;
class IngressPacket;

/**
 * @brief Creates the best available ingress backend for @p iface.
 *
 * Attempts to construct an @ref IngressXdp backend (AF_XDP). Falls back to
 * @ref IngressPacket (TPACKET_V3) if AF_XDP is unavailable or the interface
 * has no loaded XDP program.
 *
 * @param iface  Interface to receive on; must remain valid for the lifetime
 *               of the returned object.
 * @param opts   Queue configuration forwarded to the backend constructor.
 * @return Heap-allocated @ref IngressBase subclass. The caller takes ownership.
 *
 * @warning Returns @c nullptr if @p iface is @c nullptr.
 */
IngressBase* create(interface::Interface* iface, const qos::ingress::RxQueueOpts& opts);
} // namespace hardware::ingress

#endif

