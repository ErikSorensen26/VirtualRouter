// Ingress.h

#ifndef INGRESS_H
#define INGRESS_H

#include "IngressBase.h"

namespace hardware::ingress
{
class IngressXdp;
class IngressPacket;

IngressBase* create(interface::Interface* iface, const qos::ingress::RxQueueOpts& opts);
} // namespace hardware

#endif

