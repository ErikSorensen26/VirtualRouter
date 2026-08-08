// BgpRegistry.cpp

#include "BgpRegistry.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/neighbor/NeighborAf.h"
#include "bgp/af/AddressFamilyInstance.h"
#include "bgp/BgpProcess.h"

namespace config
{
using routing::bgp::Neighbor;
using routing::bgp::NeighborAf;
using routing::bgp::AfInstanceBase;
using routing::bgp::InDirty;
using routing::bgp::OutAttr;
using routing::bgp::AfDirty;

void BgpBaseRestart(void* n)
{
    static_cast<Neighbor*>(n)->enqueueConnectionRestart();
}

void BgpAfBaseAdditionalPaths(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueSyncAdditionalPaths();
}

void BgpAfBaseDefaultOriginate(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueSyncDefaultOriginate();
}

void BgpAfBaseSlowPeer(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueSyncSlowPeer();
}

void BgpNeighborActivate(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueSyncActivate();
}

void BgpNeighborAdvertiseDiverse(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueSyncAdvertiseDiverse();
}

static routing::bgp::OutAttrMask allEgressAttrs()
{
    routing::bgp::OutAttrMask m;
    m.set(static_cast<size_t>(OutAttr::NEXT_HOP));
    m.set(static_cast<size_t>(OutAttr::AS_PATH));
    m.set(static_cast<size_t>(OutAttr::COMMUNITIES));
    m.set(static_cast<size_t>(OutAttr::EXT_COMMUNITIES));
    m.set(static_cast<size_t>(OutAttr::LARGE_COMMUNITIES));
    m.set(static_cast<size_t>(OutAttr::LOCAL_PREF));
    return m;
}

void BgpNeighborAdvertiseMap(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkAttrs(allEgressAttrs());
}

void BgpNeighborAdvertiseInterval(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkAttr(OutAttr::NEXT_HOP);
}

void BgpNeighborAllowasIn(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkInbound(InDirty::ALLOWAS);
}

void BgpNeighborAnnounceRpki(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkAttr(OutAttr::COMMUNITIES);
}

void BgpNeighborInboundRefresh(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkInbound(InDirty::POLICY);
}

void BgpNeighborOutboundRefresh(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkAttrs(allEgressAttrs());
}

void BgpNeighborDmzLinkBw(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkAttr(OutAttr::EXT_COMMUNITIES);
}

void BgpNeighborRestart(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueConnectionRestart();
}

void BgpNeighborMaxPrefixRestart(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkInbound(InDirty::MAX_PREFIX);
}

void BgpNeighborNextHop(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkAttr(OutAttr::NEXT_HOP);
}

void BgpNeighborPrivateAs(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkAttr(OutAttr::AS_PATH);
}

void BgpNeighborReflector(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkAttr(OutAttr::REFLECTION);
}

void BgpNeighborSendCommunity(void* n)
{
    auto* nbr = static_cast<NeighborAf*>(n);
    routing::bgp::OutAttrMask m;
    m.set(static_cast<size_t>(OutAttr::COMMUNITIES));
    m.set(static_cast<size_t>(OutAttr::EXT_COMMUNITIES));
    m.set(static_cast<size_t>(OutAttr::LARGE_COMMUNITIES));
    nbr->enqueueMarkAttrs(m);
}

void BgpNeighborSoftReconfig(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkInbound(InDirty::SOFT_RECONFIG);
}

void BgpNeighborTranslationUpdate(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkAttrs(allEgressAttrs());
}

void BgpNeighborWeight(void* n)
{
    static_cast<NeighborAf*>(n)->enqueueMarkInbound(InDirty::POLICY);
}

void BgpNeighborSessionShutdown(void* n)
{
    static_cast<Neighbor*>(n)->enqueueSyncShutdown();
}

void BgpNeighborSessionPathAttribute(void* n)
{
    static_cast<Neighbor*>(n)->enqueueBuildAttributeRanges();
}

void BgpNeighborSessionRestart(void* n)
{
    static_cast<Neighbor*>(n)->enqueueConnectionRestart();
}

void BgpNeighborSessionRemoteAs(void* n)
{
    static_cast<Neighbor*>(n)->enqueueSyncRemoteAs();
}

void BgpNeighborSessionLocalAsPrepend(void* n)
{
    static_cast<Neighbor*>(n)->enqueueMarkAllOutbound(OutAttr::AS_PATH);
}

void BgpAfBestPath(void* a)
{
    static_cast<AfInstanceBase*>(a)->enqueueMarkAfDirty(AfDirty::BEST_PATH);
}

void BgpAfMaxPaths(void* a)
{
    static_cast<AfInstanceBase*>(a)->enqueueMarkAfDirty(AfDirty::MAX_PATHS);
}

void BgpAfDistance(void* a)
{
    static_cast<AfInstanceBase*>(a)->enqueueMarkAfDirty(AfDirty::DISTANCE);
}

void BgpAfDampening(void* a)
{
    static_cast<AfInstanceBase*>(a)->enqueueMarkAfDirty(AfDirty::DAMPENING);
}

void BgpAfAggregate(void* a)
{
    static_cast<AfInstanceBase*>(a)->enqueueMarkAfDirty(AfDirty::AGGREGATE);
}

void BgpAfNetwork(void* a)
{
    static_cast<AfInstanceBase*>(a)->enqueueSyncNetwork();
}

void BgpAfAddPathSelect(void* a)
{
    static_cast<AfInstanceBase*>(a)->enqueueMarkAfDirty(AfDirty::ADD_PATH_SELECT);
}

// ---- Process-level (cast to BgpProcess) -------------------------------------------

using routing::bgp::BgpProcess;

void BgpProcNeighbors(void* p)
{
    static_cast<BgpProcess*>(p)->enqueueSyncNeighbors();
}

void BgpProcAddressFamilies(void* p)
{
    static_cast<BgpProcess*>(p)->enqueueSyncAddressFamilies();
}

void BgpProcBestPath(void* p)
{
    static_cast<BgpProcess*>(p)->enqueueMarkAllAfDirty(AfDirty::BEST_PATH);
}

void BgpProcInbound(void* p)
{
    static_cast<BgpProcess*>(p)->enqueueMarkAllInbound(InDirty::POLICY);
}

void BgpProcReflection(void* p)
{
    static_cast<BgpProcess*>(p)->enqueueMarkAllOutbound(OutAttr::REFLECTION);
}

void BgpProcRestart(void* p)
{
    static_cast<BgpProcess*>(p)->enqueueRestartAllSessions();
}

void BgpProcConfed(void* p)
{
    static_cast<BgpProcess*>(p)->enqueueSyncConfederation();
}
}
