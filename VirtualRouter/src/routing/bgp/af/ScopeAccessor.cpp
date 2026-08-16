// ScopeAccessor.cpp

#include "ScopeAccessor.h"
#include "bgp/BgpScope.h"
#include "bgp/BgpProcess.h"

namespace routing::bgp
{
core::VirtualRouter& ScopeAccessor::getRoutingInstance(BgpScope& scope)
{
    return scope.routingInstance;
}

NeighborTable& ScopeAccessor::getNtable(BgpScope& scope)
{
    return scope.ntable;
}

uint32_t ScopeAccessor::getAsNum(BgpScope& scope)
{
    return scope.process.asNumber;
}

uint32_t ScopeAccessor::getRid(BgpScope& scope)
{
    return scope.process.getRouterId();
}

const config::BgpRegistry& ScopeAccessor::getConfigs(BgpScope& scope)
{
    return scope.configs();
}

AttributeManager& ScopeAccessor::getAttrMgr(BgpScope& scope)
{
    return scope.getAttrMgr();
}

core::ProcessQueue& ScopeAccessor::getScheduler(BgpScope& scope)
{
    return scope.scheduler;
}
} // namespace routing
