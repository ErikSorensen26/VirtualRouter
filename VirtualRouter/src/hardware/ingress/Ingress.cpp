// Ingress.cpp

#include "Ingress.h"
#include "IngressPacket.h"

namespace hardware::ingress
{
IngressBase* create(interface::Interface* iface, const qos::ingress::RxQueueOpts& opts)
{
    if (!iface) return nullptr;

    return new IngressPacket(*iface, opts);
}

} // namespace hardware
