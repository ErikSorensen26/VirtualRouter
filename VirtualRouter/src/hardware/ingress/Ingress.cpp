// Ingress.cpp

#include "Ingress.h"
#include "IngressPacket.h"

IngressBase* IngressFactory::create(Interface* iface, const RxQueueOpts& opts)
{
    if (!iface) return nullptr;

    /*try
    {
        return new IngressXdp(ifname, iface, qid, frameCount, frameSize);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[IngressFactory] XDP univailable on " << ifname
            << " (" << e.what() << "). Falling back to PACKET.\n";
    }*/

    return new IngressPacket(*iface, opts);
}
