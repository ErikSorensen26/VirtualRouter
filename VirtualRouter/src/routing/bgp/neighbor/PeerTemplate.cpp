// PeerTemplate.cpp

#include <VirtualRouter.h>

#include "PeerTemplate.h"
#include "Neighbor.h"
#include "bgp/BgpProcess.h"
#include "configs/Registry.hpp"

namespace routing::bgp
{
PeerGroup::PeerGroup(const std::string& groupName, BgpProcess& proc)
    : name(groupName),
      process(proc),
      sessionConfigs(proc.routingInstance->getRegistry().create<config::BgpNeighborSessionRegistry>())
{
    proc.routingInstance->getRegistry().ensure(
        sessionConfigs->get<config::BgpNeighborSession::BGP_BASE>(),
        proc.getConfigs().get<config::Bgp::BGP_BASE>().local()
    );
}

config::BgpNeighborRegistry* PeerGroup::getAfConfigs(const AfiSafi& afi)
{
    auto it = afConfigs.find(afi);
    if (it != afConfigs.end())
        return &it->second.get();

    auto ref = process.routingInstance->getRegistry().create<config::BgpNeighborRegistry>();
    auto [newIt, ok] = afConfigs.try_emplace(afi, std::move(ref));
    assert(ok);
    return ok ? &newIt->second.get() : nullptr;
}

const config::BgpNeighborRegistry* PeerGroup::getAfConfigs(const AfiSafi& afi) const
{
    auto it = afConfigs.find(afi);
    if (it != afConfigs.end())
        return &it->second.get();

    return const_cast<PeerGroup*>(this)->getAfConfigs(afi);
}

PeerSessionTemplate::PeerSessionTemplate(const std::string& groupName, BgpProcess& proc)
    : name(groupName),
      configs(proc.routingInstance->getRegistry().create<config::BgpNeighborSessionRegistry>())
{
    proc.routingInstance->getRegistry().ensure(
        configs->get<config::BgpNeighborSession::BGP_BASE>(),
        proc.getConfigs().get<config::Bgp::BGP_BASE>().local()
    );
}

PeerPolicyTemplate::PeerPolicyTemplate(const std::string& groupName, BgpProcess& proc)
    : name(groupName),
      configs(proc.routingInstance->getRegistry().create<config::BgpNeighborRegistry>())
{}

// ---------------------------------------------------------------------------
// PeerTemplateTable
// ---------------------------------------------------------------------------

PeerTemplateTable::PeerTemplateTable(BgpProcess& proc)
    : process(proc)
{}

void PeerTemplateTable::sync()
{
    syncPeerGroups();
    syncPeerSessionTemplates();
    syncPeerPolicyTemplates();
}

void PeerTemplateTable::syncPeerGroups()
{
    auto& ntable = process.getNtable();
    ntable.forEachNeighbor([&](Neighbor& nbr) {
        auto& pgField = nbr.getConfigs().getConfigs()->get<config::BgpNeighborSession::PEER_GROUP>();
        if (pgField.hasValue())
        {
            auto* pg = lookupPeerGroup(pgField.load());
            nbr.getConfigs().setPeerGroup(pg);
            nbr.forEachAfNeighbor([&pg](NeighborAf& afNbr) {
                afNbr.getConfigs().setPeerGroup(pg);
            });
        }
        else if (nbr.getConfigs().getPeerGroup())
        {
            nbr.getConfigs().setPeerGroup(nullptr);
            nbr.forEachAfNeighbor([](NeighborAf& afNbr) {
                afNbr.getConfigs().setPeerGroup(nullptr);
            });
        }
    });
}

void PeerTemplateTable::syncPeerSessionTemplates()
{
    auto& ntable = process.getNtable();
    ntable.forEachNeighbor([&](Neighbor& nbr) {
        auto& f = nbr.getConfigs().getConfigs()->get<config::BgpNeighborSession::INHERIT_PEER_SESSION>();
        if (f.hasValue())
        {
            auto* ps = lookupPeerSessionTemplate(f.load());
            nbr.getConfigs().setPeerSessionTemplate(ps);
        }
        else if (nbr.getConfigs().getPeerSessionTemplate())
        {
            nbr.getConfigs().setPeerSessionTemplate(nullptr);
        }
    });
}

void PeerTemplateTable::syncPeerPolicyTemplates()
{
    auto& ntable = process.getNtable();
    ntable.forEachNeighbor([&](Neighbor& nbr) {
        nbr.forEachAfNeighbor([this](NeighborAf& afNbr) {
            auto& f = afNbr.getConfigs().getConfigs().get<config::BgpNeighbor::INHERIT_PEER_POLICY>();
            if (f.hasValue())
            {
                auto* pp = lookupPeerPolicyTemplate(f.load());
                afNbr.getConfigs().setPeerPolicyTemplate(pp);
            }
            else
            {
                afNbr.getConfigs().setPeerPolicyTemplate(nullptr);
            }
        });
    });
}

PeerGroup& PeerTemplateTable::createPeerGroup(const std::string& name)
{
    auto [it, _] = peerGroups.try_emplace(name, name, process);
    return it->second;
}

PeerSessionTemplate& PeerTemplateTable::createPeerSessionTemplate(const std::string& name)
{
    auto [it, _] = peerSessionTemplates.try_emplace(name, name, process);
    return it->second;
}

PeerPolicyTemplate& PeerTemplateTable::createPeerPolicyTemplate(const std::string& name)
{
    auto [it, _] = peerPolicyTemplates.try_emplace(name, name, process);
    return it->second;
}

void PeerTemplateTable::removePeerGroup(const std::string& name)
{
    peerGroups.erase(name);
    syncPeerGroups();
}

void PeerTemplateTable::removePeerSessionTemplate(const std::string& name)
{
    peerSessionTemplates.erase(name);
    syncPeerSessionTemplates();
}

void PeerTemplateTable::removePeerPolicyTemplate(const std::string& name)
{
    peerPolicyTemplates.erase(name);
    syncPeerPolicyTemplates();
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
