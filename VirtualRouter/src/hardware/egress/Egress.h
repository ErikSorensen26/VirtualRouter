// Egress.h

#ifndef EGRESS_H
#define EGRESS_H

#include "EgressBase.h"

namespace qos::egress { struct TxQueueOpts; }

namespace hardware::egress
{
class EgressXdp;
class EgressPacket;

inline static EgressBase* create(interface::Interface* iface, const qos::egress::TxQueueOpts& opts);
} // namespace hardware

#endif

