// Ingress.cpp

#include <stdexcept>
#include "Ingress.h"
#include "IngressXdp.h"
#include "IngressPacket.h"

namespace hardware::ingress
{

IngressBase* create(interface::Interface* iface, const qos::ingress::RxQueueOpts& opts)
{
    if (!iface) return nullptr;

    /*try
    {
        return new IngressXdp(*iface, opts);
    }
    catch (const std::exception&)*/
    {
        return new IngressPacket(*iface, opts);
    }
}

} // namespace hardware::ingress
