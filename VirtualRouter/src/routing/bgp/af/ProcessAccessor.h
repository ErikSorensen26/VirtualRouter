// ProcessAccessor.h

#ifndef BGP_PROCESS_ACCESSOR_H
#define BGP_PROCESS_ACCESSOR_H

#include <cstdint>

#include "configs/registry/router/BgpRegistry.h"

namespace core { class VirtualRouter; class ProcessQueueRef; }

namespace routing::bgp
{
class NeighborTable;
class BgpProcess;
class Neighbor;
class AttributeManager;

class ProcessAccessor
{
public:
    static core::VirtualRouter& getRoutingInstance(BgpProcess& proc);
    static NeighborTable& getNtable(BgpProcess& proc);
    static uint32_t getAsNum(BgpProcess& proc);
    static uint32_t getRid(BgpProcess& proc);
    static config::BgpRegistry& getConfigs(BgpProcess& proc);
    static AttributeManager& getAttrMgr(BgpProcess& proc);
    static core::ProcessQueueRef getScheduler(BgpProcess& proc);
    static void emplaceAfBase(config::ReferenceContainer<config::BgpAfBaseRegistry CONFIG_INDEX_PARAM>& base, BgpProcess& proc);
};
} // namespace routing

#endif // BGP_PROCESS_ACCESSOR_H

