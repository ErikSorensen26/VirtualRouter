// Ingress.h

#ifndef INGRESS_H
#define INGRESS_H

#include "IngressBase.h"

class IngressXdp;
class IngressPacket;

struct RxQueueOpts;

class IngressFactory
{
public:
    static IngressBase* create(Interface* iface, const RxQueueOpts& opts);
};

#endif
