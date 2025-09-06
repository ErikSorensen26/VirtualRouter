// Egress.h

#ifndef EGRESS_H
#define EGRESS_H

#include "EgressBase.h"

class EgressXdp;
class EgressPacket;

struct TxQueueOpts;

class EgressFactory
{
public:
    static EgressBase* create(Interface* iface, const TxQueueOpts& opts);
};

#endif
