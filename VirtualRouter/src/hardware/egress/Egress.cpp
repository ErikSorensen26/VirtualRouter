// Egress.cpp


#include "Egress.h"
#include "EgressSend.h"

namespace hardware::egress
{
EgressBase* create(interface::Interface* iface, const qos::egress::TxQueueOpts& opts)
{
    if (!iface) return nullptr;

    return new EgressSend(*iface, opts);
    //return new EgressPacket(*iface, opts);
}

} // namespace hardware
