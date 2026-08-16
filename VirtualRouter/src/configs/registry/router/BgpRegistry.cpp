// BgpRegistry.cpp

#include "BgpRegistry.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/neighbor/NeighborAf.h"
#include "bgp/af/AddressFamilyInstance.h"
#include "bgp/BgpProcess.h"

namespace config
{
DEFINE_CONFIG_APPLIER(BgpTransportBase, KEEPALIVE_INTERVAL, ctx, keepalive)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpTransportBase, HOLDTIME, ctx, holdtime)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpTransportBase, MINIMUM_HOLDTIME, ctx, minHoldtime)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpTransportBase, TRANSPORT_PATH_MTU_DISCOVERY, ctx, pmtud)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpAfBase, ADDITIONAL_PATHS_RECEIVE, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncAdditionalPaths();
    else if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::ADD_PATH_SELECT);
}

DEFINE_CONFIG_APPLIER(BgpAfBase, ADDITIONAL_PATHS_SEND, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncAdditionalPaths();
    else if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::ADD_PATH_SELECT);
}

DEFINE_CONFIG_APPLIER(BgpAfBase, ADDITIONAL_PATHS_SELECT_ALL, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncAdditionalPaths();
    else if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::ADD_PATH_SELECT);
}

DEFINE_CONFIG_APPLIER(BgpAfBase, ADDITIONAL_PATHS_SELECT_BACKUP, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncAdditionalPaths();
    else if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::ADD_PATH_SELECT);
}

DEFINE_CONFIG_APPLIER(BgpAfBase, ADDITIONAL_PATHS_SELECT_BEST, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncAdditionalPaths();
    else if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::ADD_PATH_SELECT);
}

DEFINE_CONFIG_APPLIER(BgpAfBase, ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncAdditionalPaths();
    else if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::ADD_PATH_SELECT);
}

DEFINE_CONFIG_APPLIER(BgpAfBase, ADDITIONAL_PATHS_SELECT_GROUP_BEST, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncAdditionalPaths();
    else if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::ADD_PATH_SELECT);
}

DEFINE_CONFIG_APPLIER(BgpAfBase, DEFAULT_ORIGINATE, ctx, originate)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncDefaultOriginate(originate);
}

DEFINE_CONFIG_APPLIER(BgpAfBase, SLOW_PEER_MODE, ctx, mode)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncSlowPeer();
}

DEFINE_CONFIG_APPLIER(BgpAfBase, SLOW_PEER_DETECTION, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncSlowPeer();
}

DEFINE_CONFIG_APPLIER(BgpAfBase, SLOW_PEER_DETECTION_THRESHOLD, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncSlowPeer();
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, ACTIVATE, ctx, active)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncActivate(active);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, ADVERTISE_DIVERSE_PATH_BACKUP, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncAdvertiseDiverse();
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, ADVERTISE_DIVERSE_PATH_MPATH, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncAdvertiseDiverse();
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, ADVERTISE_INTERVAL, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkAttr(routing::bgp::OutAttr::NEXT_HOP);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, ALLOWAS_IN, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkInbound(routing::bgp::InDirty::ALLOWAS);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, ALLOWAS_IN_OCCURANCES, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkInbound(routing::bgp::InDirty::ALLOWAS);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, ORF_BOTH, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, ORF_RECEIVE, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, ORF_SEND, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, INHERIT_PEER_POLICY, ctx, name)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueSyncPeerPolicyTemplate(name ? std::optional{*name} : std::nullopt);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, MAXIMUM_PREFIX, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkInbound(routing::bgp::InDirty::MAX_PREFIX);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, MAXIMUM_PREFIX_THRESHOLD, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkInbound(routing::bgp::InDirty::MAX_PREFIX);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, MAXIMUM_PREFIX_RESTART, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkInbound(routing::bgp::InDirty::MAX_PREFIX);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, MAXIMUM_PREFIX_WARNING_ONLY, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkInbound(routing::bgp::InDirty::MAX_PREFIX);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, NEXT_HOP_SELF, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkAttr(routing::bgp::OutAttr::NEXT_HOP);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, NEXT_HOP_SELF_ALL, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkAttr(routing::bgp::OutAttr::NEXT_HOP);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, NEXT_HOP_UNCHANGED, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkAttr(routing::bgp::OutAttr::NEXT_HOP);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, REMOVE_PRIVATE_AS, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkAttr(routing::bgp::OutAttr::AS_PATH);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, ROUTE_REFLECTOR_CLIENT, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkAttr(routing::bgp::OutAttr::REFLECTION);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, SEND_COMMUNITY, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
    {
        routing::bgp::OutAttrMask m;
        m.set(static_cast<size_t>(routing::bgp::OutAttr::COMMUNITIES));
        m.set(static_cast<size_t>(routing::bgp::OutAttr::EXT_COMMUNITIES));
        m.set(static_cast<size_t>(routing::bgp::OutAttr::LARGE_COMMUNITIES));
        nbr->enqueueMarkAttrs(m);
    }
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, SEND_COMMUNITY_BOTH, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
    {
        routing::bgp::OutAttrMask m;
        m.set(static_cast<size_t>(routing::bgp::OutAttr::COMMUNITIES));
        m.set(static_cast<size_t>(routing::bgp::OutAttr::EXT_COMMUNITIES));
        m.set(static_cast<size_t>(routing::bgp::OutAttr::LARGE_COMMUNITIES));
        nbr->enqueueMarkAttrs(m);
    }
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, SEND_COMMUNITY_EXTENDED, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
    {
        routing::bgp::OutAttrMask m;
        m.set(static_cast<size_t>(routing::bgp::OutAttr::COMMUNITIES));
        m.set(static_cast<size_t>(routing::bgp::OutAttr::EXT_COMMUNITIES));
        m.set(static_cast<size_t>(routing::bgp::OutAttr::LARGE_COMMUNITIES));
        nbr->enqueueMarkAttrs(m);
    }
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, SEND_COMMUNITY_STANDARD, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
    {
        routing::bgp::OutAttrMask m;
        m.set(static_cast<size_t>(routing::bgp::OutAttr::COMMUNITIES));
        m.set(static_cast<size_t>(routing::bgp::OutAttr::EXT_COMMUNITIES));
        m.set(static_cast<size_t>(routing::bgp::OutAttr::LARGE_COMMUNITIES));
        nbr->enqueueMarkAttrs(m);
    }
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, SEND_LABEL, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, SEND_LABEL_EXPLICIT_NULL, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, SOFT_RECONFIGURATION, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkInbound(routing::bgp::InDirty::SOFT_RECONFIG);
}

DEFINE_CONFIG_APPLIER(BgpNeighbor, WEIGHT, ctx, val)
{
    if (routing::bgp::NeighborAf* nbr = Context::cast<routing::bgp::NeighborAf*>(ctx); nbr)
        nbr->enqueueMarkInbound(routing::bgp::InDirty::POLICY);
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, SHUTDOWN, ctx, shutdown)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueSyncShutdown(shutdown);
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, PATH_ATTRIBUTE_DISCARD, ctx, val, add)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueBuildAttributeRanges();
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, PATH_ATTRIBUTE_TREAT_AS_WITHDRAW, ctx, val, add)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueBuildAttributeRanges();
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, DISABLE_CONNECTION_CHECK, ctx, val)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, TRANSPORT_CONNECTION_MODE, ctx, val)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, TRANSPORT_MULTI_SESSION, ctx, val)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, UPDATE_SOURCE, ctx, val)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueConnectionRestart();
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, REMOTE_AS, ctx, remoteAs)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueSyncRemoteAs(remoteAs ? std::optional{*remoteAs} : std::nullopt);
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, LOCAL_AS, ctx, val)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueMarkAllOutbound(routing::bgp::OutAttr::AS_PATH);
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, PEER_GROUP, ctx, name)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueSyncPeerGroup(name ? std::optional{*name} : std::nullopt);
}

DEFINE_CONFIG_APPLIER(BgpNeighborSession, INHERIT_PEER_SESSION, ctx, name)
{
    if (routing::bgp::Neighbor* nbr = Context::cast<routing::bgp::Neighbor*>(ctx); nbr)
        nbr->enqueueSyncPeerSessionTemplate(name ? std::optional{*name} : std::nullopt);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, BGP_BEST_PATH_IGP_METRIC_IGNORE, ctx, val)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::BEST_PATH);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, MAXIMUM_PATHS_EBGP, ctx, val)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::MAX_PATHS);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, MAXIMUM_PATHS_IBGP, ctx, val)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::MAX_PATHS);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, DISTANCE_RANGE, ctx, val, add)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::DISTANCE);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, DISTANCE_BGP_EXTERNAL, ctx, val)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::DISTANCE);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, DISTANCE_BGP_INTERNAL, ctx, val)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::DISTANCE);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, DISTANCE_BGP_LOCAL, ctx, val)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::DISTANCE);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, DISTANCE_MBGP_EXTERNAL, ctx, val)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::DISTANCE);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, DISTANCE_MBGP_INTERNAL, ctx, val)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::DISTANCE);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, DISTANCE_MBGP_LOCAL, ctx, val)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::DISTANCE);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, AGGREGATE_ADDRESS, ctx, val, add)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::AGGREGATE);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, BGP_AGGREGATE_TIMER, ctx, val)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueMarkAfDirty(routing::bgp::AfDirty::AGGREGATE);
}

DEFINE_CONFIG_APPLIER(BgpAddressFamily, NETWORK, ctx, val, add)
{
    if (routing::bgp::AfInstanceBase* af = Context::cast<routing::bgp::AfInstanceBase*>(ctx); af)
        af->enqueueSyncNetwork();
}

// ---- Process-level (cast to BgpProcess) -------------------------------------------

using routing::bgp::BgpProcess;

DEFINE_CONFIG_APPLIER(Bgp, NEIGHBOR, ctx, reg, addr)
{
    if (BgpProcess* p = Context::cast<routing::bgp::BgpProcess*>(ctx); p)
        p->enqueueNeighbor(reg, addr);
}

DEFINE_CONFIG_APPLIER(Bgp, PEER_GROUP, ctx, reg, grp)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueSyncPeerGroup(reg, grp);
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_ALWAYS_COMPARE_MED, ctx, val)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueMarkAllAfDirty(routing::bgp::AfDirty::BEST_PATH);
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_BEST_PATH_COMPARE_ROUTER_ID, ctx, val)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueMarkAllAfDirty(routing::bgp::AfDirty::BEST_PATH);
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_BEST_PATH_MED_MISSING_AS_WORST, ctx, val)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueMarkAllAfDirty(routing::bgp::AfDirty::BEST_PATH);
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_DETERMINISTIC_MED, ctx, val)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueMarkAllAfDirty(routing::bgp::AfDirty::BEST_PATH);
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_ENFORCE_FIRST_AS, ctx, val)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueMarkAllInbound(routing::bgp::InDirty::POLICY);
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_SUPPRESS_INACTIVE, ctx, val)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueMarkAllInbound(routing::bgp::InDirty::POLICY);
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_CLIENT_TO_CLIENT_REFLECTION, ctx, val)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueMarkAllOutbound(routing::bgp::OutAttr::REFLECTION);
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_CLUSTER_ID, ctx, val)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueMarkAllOutbound(routing::bgp::OutAttr::REFLECTION);
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_ROUTER_ID, ctx, val)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueRestartAllSessions();
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_CONFEDERATION_IDENTIFIER, ctx, val)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueSyncConfederation();
}

DEFINE_CONFIG_APPLIER(Bgp, BGP_CONFEDERATION_PEERS, ctx, val, add)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueSyncConfederation();
}

DEFINE_CONFIG_APPLIER(Bgp, AF_VRF, ctx, reg, vrf)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
    {
        reg->context().set(p);
        reg->get<BgpAfVrf::VRF>().set(vrf);
    }
}

DEFINE_CONFIG_APPLIER(BgpAfVrf, IPV4_UNICAST, ctx, reg)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueAddressFamily({BGP_AFI_IPV4, BGP_SAFI_UNICAST}, reg, reg->resolveParent<config::BgpAfVrfRegistry>()->get<config::BgpAfVrf::VRF>().load());
}

DEFINE_CONFIG_APPLIER(BgpAfVrf, IPV6_UNICAST, ctx, reg)
{
    if (BgpProcess* p = Context::cast<BgpProcess*>(ctx); p)
        p->enqueueAddressFamily({BGP_AFI_IPV6, BGP_SAFI_UNICAST}, reg, reg->resolveParent<config::BgpAfVrfRegistry>()->get<config::BgpAfVrf::VRF>().load());
}
}
