// Egress.h

#ifndef EGRESS_H
#define EGRESS_H

#include "EgressBase.h"

namespace qos::egress { struct TxQueueOpts; }

namespace hardware::egress
{
class EgressPacket;

EgressBase* create(interface::Interface* iface, const qos::egress::TxQueueOpts& opts);
} // namespace hardware

#endif

