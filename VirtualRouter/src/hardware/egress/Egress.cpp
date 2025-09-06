// Egress.cpp

#include "Egress.h"
#include "EgressPacket.h"
#include <iostream>
#include <exception>

EgressBase* EgressFactory::create(Interface* iface, const TxQueueOpts& opts)
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

    return new EgressPacket(*iface, opts);
}
