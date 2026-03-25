// Egress.cpp

#include <stdexcept>
#include "Egress.h"
#include "EgressPacket.h"
#include "EgressSend.h"

namespace hardware::egress
{
EgressBase* create(interface::Interface* iface, const qos::egress::TxQueueOpts& opts)
{
    if (!iface) return nullptr;

    try
    {
        return new EgressPacket(*iface, opts);
    }
    catch (const std::exception&)
    {
        return new EgressSend(*iface, opts);
    }
}

} // namespace hardware
