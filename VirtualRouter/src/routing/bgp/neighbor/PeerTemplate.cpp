// PeerTemplate.cpp

#include <VirtualRouter.h>

#include "PeerTemplate.h"
#include "Neighbor.h"
#include "bgp/BgpProcess.h"
#include "bgp/BgpScope.h"

namespace routing::bgp
{
PeerGroup::PeerGroup(const std::string& groupName, BgpProcess& proc, config::BgpNeighborSessionRegistry& cfgs)
    : name(groupName),
      process(proc),
      sessionConfigs(cfgs)
{}

config::BgpNeighborRegistry* PeerGroup::getAfConfigs(const AfiSafi& afi)
{
    auto it = afConfigs.find(afi);
    if (it != afConfigs.end())
        return &it->second;

    auto [newIt, ok] = afConfigs.try_emplace(afi);
    assert(ok);
    return ok ? &newIt->second : nullptr;
}

const config::BgpNeighborRegistry* PeerGroup::getAfConfigs(const AfiSafi& afi) const
{
    auto it = afConfigs.find(afi);
    if (it != afConfigs.end())
        return &it->second;

    return const_cast<PeerGroup*>(this)->getAfConfigs(afi);
}

PeerSessionTemplate::PeerSessionTemplate(const std::string& groupName, config::BgpNeighborSessionRegistry& cfgs)
    : name(groupName),
      configs(cfgs)
{}

PeerPolicyTemplate::PeerPolicyTemplate(const std::string& groupName, config::BgpNeighborRegistry& cfgs)
    : name(groupName),
      configs(cfgs)
{}

// ---------------------------------------------------------------------------
// PeerTemplateTable
// ---------------------------------------------------------------------------

PeerTemplateTable::PeerTemplateTable(BgpProcess& proc)
    : process(proc)
{}

void PeerTemplateTable::syncNeighborPeerGroup(Neighbor& nbr)
{
    if (nbr.getDynamic()) return;

    auto pgField = nbr.configs.get<config::BgpNeighborSession::PEER_GROUP>();
    if (pgField.hasValue())
    {
        auto* pg = lookupPeerGroup(pgField.load());
        nbr.configs.setPeerGroup(pg);
        nbr.forEachAfNeighbor([&pg](NeighborAf& afNbr) {
            afNbr.configs.setPeerGroup(pg);
        });
    }
    else if (nbr.configs.getPeerGroup())
    {
        nbr.configs.setPeerGroup(nullptr);
        nbr.forEachAfNeighbor([](NeighborAf& afNbr) {
            afNbr.configs.setPeerGroup(nullptr);
        });
    }
}

void PeerTemplateTable::syncNeighborPeerGroup(NeighborAf& afNbr)
{
    if (afNbr.parent.getDynamic()) return;

    auto pgField = afNbr.parent.configs.get<config::BgpNeighborSession::PEER_GROUP>();
    if (pgField.hasValue())
    {
        auto* pg = lookupPeerGroup(pgField.load());
        afNbr.configs.setPeerGroup(pg);
    }
    else if (afNbr.configs.getPeerGroup())
    {
        afNbr.configs.setPeerGroup(nullptr);
    }
}

void PeerTemplateTable::syncNeighborPeerSessionTemplate(Neighbor& nbr)
{
    if (nbr.getDynamic()) return;

    auto f = nbr.configs.getConfigs().get<config::BgpNeighborSession::INHERIT_PEER_SESSION>();
    if (f.hasValue())
    {
        auto* ps = lookupPeerSessionTemplate(f.load());
        nbr.configs.setPeerSessionTemplate(ps);
    }
    else if (nbr.configs.getPeerSessionTemplate())
    {
        nbr.configs.setPeerSessionTemplate(nullptr);
    }
}

void PeerTemplateTable::syncNeighborPeerPolicyTemplate(NeighborAf& afNbr)
{
    if (afNbr.parent.getDynamic()) return;

    auto f = afNbr.configs.getConfigs().get<config::BgpNeighbor::INHERIT_PEER_POLICY>();
    if (f.hasValue())
    {
        auto* pp = lookupPeerPolicyTemplate(f.load());
        afNbr.configs.setPeerPolicyTemplate(pp);
    }
    else
    {
        afNbr.configs.setPeerPolicyTemplate(nullptr);
    }
}

void PeerTemplateTable::syncPeerGroup(const std::string& group)
{
    PeerGroup* grp = lookupPeerGroup(group);
    for (auto& [_, scope] : process.scopes)
    {
        auto& ntable = scope.ntable;
        ntable.forEachNeighbor([&](Neighbor& nbr) {
            if (nbr.getDynamic()) return;
            auto pgField = nbr.configs.get<config::BgpNeighborSession::PEER_GROUP>();
            if (!pgField.hasValue() || pgField.load() != group)
                return;
            nbr.configs.setPeerGroup(grp);
            nbr.forEachAfNeighbor([grp](NeighborAf& afNbr) {
                afNbr.configs.setPeerGroup(grp);
            });
        });
    }
}

void PeerTemplateTable::syncPeerSessionTemplate(const std::string& sess)
{
    PeerSessionTemplate* sessTemp = lookupPeerSessionTemplate(sess);
    for (auto& [_, scope] : process.scopes)
    {
        auto& ntable = scope.ntable;
        ntable.forEachNeighbor([&](Neighbor& nbr) {
            auto f = nbr.configs.getConfigs().get<config::BgpNeighborSession::INHERIT_PEER_SESSION>();
            if (!f.hasValue() || f.load() != sess)
                return;
            nbr.configs.setPeerSessionTemplate(sessTemp);
        });
    }
}

void PeerTemplateTable::syncPeerPolicyTemplate(const std::string& policy)
{
    PeerPolicyTemplate* policyTemp = lookupPeerPolicyTemplate(policy);
    for (auto& [_, scope] : process.scopes)
    {
        auto& ntable = scope.ntable;
        ntable.forEachNeighbor([&](Neighbor& nbr) {
            nbr.forEachAfNeighbor([&](NeighborAf& afNbr) {
                auto f = afNbr.configs.getConfigs().get<config::BgpNeighbor::INHERIT_PEER_POLICY>();
                if (!f.hasValue() || f.load() != policy)
                    return;
                afNbr.configs.setPeerPolicyTemplate(policyTemp);
            });
        });
    }
}

PeerGroup& PeerTemplateTable::createPeerGroup(config::BgpNeighborSessionRegistry& configs, const std::string& name)
{
    auto [it, _] = peerGroups.try_emplace(name, name, process, configs);
    return it->second;
}

PeerSessionTemplate& PeerTemplateTable::createPeerSessionTemplate(config::BgpNeighborSessionRegistry& configs, const std::string& name)
{
    auto [it, _] = peerSessionTemplates.try_emplace(name, name, configs);
    return it->second;
}

PeerPolicyTemplate& PeerTemplateTable::createPeerPolicyTemplate(config::BgpNeighborRegistry& configs, const std::string& name)
{
    auto [it, _] = peerPolicyTemplates.try_emplace(name, name, configs);
    return it->second;
}

void PeerTemplateTable::removePeerGroup(const std::string& name)
{
    if (PeerGroup* group = lookupPeerGroup(name); group)
    {
        for (auto& [_, scope] : process.scopes)
            scope.ntable.purgeDynamicNeighbors(group);
    }
    peerGroups.erase(name);
    syncPeerGroup(name);
}

void PeerTemplateTable::removePeerSessionTemplate(const std::string& name)
{
    peerSessionTemplates.erase(name);
    syncPeerSessionTemplate(name);
}

void PeerTemplateTable::removePeerPolicyTemplate(const std::string& name)
{
    peerPolicyTemplates.erase(name);
    syncPeerPolicyTemplate(name);
}

PeerGroup* PeerTemplateTable::lookupPeerGroup(const std::string& name)
{
    auto it = peerGroups.find(name);
    return (it != peerGroups.end()) ? &it->second : nullptr;
}

const PeerGroup* PeerTemplateTable::lookupPeerGroup(const std::string& name) const
{
    auto it = peerGroups.find(name);
    return (it != peerGroups.end()) ? &it->second : nullptr;
}

PeerSessionTemplate* PeerTemplateTable::lookupPeerSessionTemplate(const std::string& name)
{
    auto it = peerSessionTemplates.find(name);
    return (it != peerSessionTemplates.end()) ? &it->second : nullptr;
}

const PeerSessionTemplate* PeerTemplateTable::lookupPeerSessionTemplate(const std::string& name) const
{
    auto it = peerSessionTemplates.find(name);
    return (it != peerSessionTemplates.end()) ? &it->second : nullptr;
}

PeerPolicyTemplate* PeerTemplateTable::lookupPeerPolicyTemplate(const std::string& name)
{
    auto it = peerPolicyTemplates.find(name);
    return (it != peerPolicyTemplates.end()) ? &it->second : nullptr;
}

const PeerPolicyTemplate* PeerTemplateTable::lookupPeerPolicyTemplate(const std::string& name) const
{
    auto it = peerPolicyTemplates.find(name);
    return (it != peerPolicyTemplates.end()) ? &it->second : nullptr;
}
} // namespace routing::bgp
