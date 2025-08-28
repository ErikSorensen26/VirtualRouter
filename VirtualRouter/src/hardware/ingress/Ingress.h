// Ingress.h

#ifndef INGRESS_H
#define INGRESS_H

#include "IngressBase.h"

class IngressXdp;
class IngressPacket;

class IngressFactory
{
public:
    static IngressBase* create(const char* ifname, Interface& iface, uint32_t qid, uint32_t frameCount, uint32_t frameSize, uint32_t pktSnapLen = 2048);
};

#endif
