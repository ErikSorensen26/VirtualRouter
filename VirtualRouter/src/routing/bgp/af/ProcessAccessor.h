// ProcessAccessor.h

#ifndef BGP_PROCESS_ACCESSOR_H
#define BGP_PROCESS_ACCESSOR_H

#include <cstdint>

#include "configs/registry/router/BgpRegistry.h"

class VirtualRouter;
class ProcessQueueRef;

namespace BGP
{
class NeighborTable;
class BgpProcess;
class Neighbor;
class AttributeManager;

class ProcessAccessor
{
public:
    static VirtualRouter& getRoutingInstance(BgpProcess& proc);
    static NeighborTable& getNtable(BgpProcess& proc);
    static uint32_t getAsNum(BgpProcess& proc);
    static uint32_t getRid(BgpProcess& proc);
    static Config::BgpRegistry& getConfigs(BgpProcess& proc);
    static AttributeManager& getAttrMgr(BgpProcess& proc);
    static ProcessQueueRef getScheduler(BgpProcess& proc);
    static void emplaceAfBase(Config::ReferenceContainer<Config::BgpAfBaseRegistry CONFIG_INDEX_PARAM>& base, BgpProcess& proc);
};
}

#endif // BGP_PROCESS_ACCESSOR_H
