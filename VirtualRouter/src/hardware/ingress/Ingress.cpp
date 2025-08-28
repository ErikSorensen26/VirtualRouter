// Ingress.cpp

#include "Ingress.h"
#include "IngressXdp.h"
#include "IngressPacket.h"
#include <iostream>
#include <exception>

IngressBase* IngressFactory::create(const char* ifname, Interface& iface, uint32_t qid, uint32_t frameCount, uint32_t frameSize, uint32_t pktSnapLen)
{
    try
    {
        return new IngressXdp(ifname, iface, qid, frameCount, frameSize);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[IngressFactory] XDP univailable on " << ifname
            << " (" << e.what() << "). Falling back to PACKET.\n";
    }

    return new IngressPacket(ifname, iface, qid, frameCount, pktSnapLen);
}
