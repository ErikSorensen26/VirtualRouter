// BgpProcess.cpp

#include <Global.h>
#include <VirtualRouter.h>
#include "BgpProcess.h"
#include "BgpScope.h"

namespace routing::bgp
{
BgpProcess::BgpProcess(const config::BgpRegistry& reg, uint32_t as, core::Global& g)
    : global(g),
      asNumber(as),
      peerTemplates(*this),
      configs(reg)
{
    core::VirtualRouter& defVrf = *global.getRoutingInstance(DEFAULT_VRF);

    auto ridField = configs.get<config::Bgp::BGP_ROUTER_ID>();
    if (ridField.hasValue())
        rid = ridField.load();
    else
        defVrf.calculateRID(rid);

    configs.get<config::Bgp::AUTONOMOUS_SYSTEM>().set(as);
    configs.context().set(this);
}

BgpProcess::~BgpProcess()
{
    configs.context().reset();
    scopes.clear();
}

uint32_t BgpProcess::getRouterId() const
{
    return rid;
}

void BgpProcess::enqueueNeighbor(config::BgpNeighborSessionRegistry* cfgs, const types::IPAddress& addr)
{
    for (auto& [_, scope] : scopes)
    {
        scope.scheduler.post([&scope, cfgs, addr]() {
            if (cfgs)
            {
                if (Neighbor* nbr = scope.ntable.createNeighbor(*cfgs, addr))
                    scope.ntable.startConfiguredSession(*nbr);
            }
            else
                scope.ntable.deleteNeighbor(addr);
        });
    }
}

void BgpProcess::enqueueSyncPeerGroup(config::BgpNeighborSessionRegistry* cfgs, const std::string& peerGroup)
{
    if (cfgs)
        peerTemplates.createPeerGroup(*cfgs, peerGroup);
    else
        peerTemplates.removePeerGroup(peerGroup);
}

void BgpProcess::enqueueSyncPeerSessionTemplate(config::BgpNeighborSessionRegistry* cfgs, const std::string& sess)
{
    if (cfgs)
        peerTemplates.createPeerSessionTemplate(*cfgs, sess);
    else
        peerTemplates.removePeerSessionTemplate(sess);
}

void BgpProcess::enqueueSyncPeerPolicyTemplate(config::BgpNeighborRegistry* cfgs, const std::string& policy)
{
    if (cfgs)
        peerTemplates.createPeerPolicyTemplate(*cfgs, policy);
    else
        peerTemplates.removePeerPolicyTemplate(policy);
}

void BgpProcess::enqueueAddressFamily(const AfiSafi& afiSafi, config::BgpAddressFamilyRegistry* cfgs, const std::string& vrfName)
{
    auto it = scopes.find(vrfName);
    if (it == scopes.end())
    {
        if (!cfgs) return;
        core::VirtualRouter* vrf = global.getRoutingInstance(vrfName);
        auto [scop, _] = scopes.try_emplace(vrfName, *this, *vrf);
        it = scop;
    }

    BgpScope& scope = it->second;
    scope.scheduler.post([&scope, cfgs, afiSafi]() {
        if (cfgs)
            scope.enableAddressFamily(afiSafi, *cfgs);
        else
            scope.disableAddressFamily(afiSafi);
    });

    if (!cfgs && it->second.addressFamilies.empty())
        scopes.erase(it);
}

void BgpProcess::enqueueMarkAllAfDirty(AfDirty category)
{
    for (auto& [_, scope] : scopes)
    {
        scope.scheduler.post([&scope, category]() {
            for (auto& [afi, af] : scope.addressFamilies)
                std::visit([category](auto& fam) { fam.markAfDirty(category); }, af);
        });
    }
}

void BgpProcess::enqueueMarkAllInbound(InDirty category)
{
    for (auto& [_, scope] : scopes)
    {
        scope.scheduler.post([&scope, category]() {
            for (auto& [afi, af] : scope.addressFamilies)
                std::visit([category](auto& fam) { fam.markInboundDirty(category, 0); }, af);
        });
    }
}

void BgpProcess::enqueueMarkAllOutbound(OutAttr attr)
{
    for (auto& [_, scope] : scopes)
    {
        scope.scheduler.post([&scope, attr]() {
            scope.ntable.forEachNeighbor([&](Neighbor& nbr) {
                nbr.forEachAfNeighbor([&](NeighborAf& afNbr) { afNbr.markAttr(attr); });
            });
        });
    }
}

void BgpProcess::enqueueRestartAllSessions()
{
    for (auto& [_, scope] : scopes)
    {
        scope.scheduler.post([&scope]() {
            scope.ntable.forEachNeighbor([&scope](Neighbor& nbr) {
                scope.ntable.restartNeighbor(nbr);
            });
        });
    }
}

void BgpProcess::enqueueSyncConfederation()
{
    for (auto& [_, scope] : scopes)
    {
        scope.scheduler.post([&scope]() {
            scope.ntable.forEachNeighbor([&scope](Neighbor& nbr) {
                nbr.syncClassification();
                scope.ntable.restartNeighbor(nbr);
            });
        });
    }
}
}
