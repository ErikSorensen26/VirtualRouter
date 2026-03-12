// NeighborAf.cpp

#include <algorithm>
#include "NeighborAf.h"
#include "Neighbor.h"
#include "PeerTemplate.h"

#include "bgp/BgpProcess.h"

namespace BGP
{
NeighborAf::NeighborAf(const AfiSafi& fam, Neighbor& p)
    : family(fam),
      mpNegotiated(false),
      parent(p),
      configs(fam, [&p, &fam]() {
          auto& neighborConfigs = p.configs.get<Config::BgpNeighborSession::AF_NEIGHBOR>();
          auto key = Config::generateBgpNeighborKey(p.configs.getConfigs().getKey(), fam.afi, fam.safi);
          uint32_t id = fam.afi | uint32_t(fam.afi) << 16;
          return p.getProcess().routingInstance->getRegistry().emplaceBack(neighborConfigs, id, key);
      }())
{
    // Resolve peer group
    {
        auto& pgField = parent.getConfigs().get<Config::BgpNeighborSession::PEER_GROUP>();
        if (pgField.hasValue())
            configs.setPeerGroup(parent.getProcess().getNtable().lookupPeerGroup(pgField.load()));
    }

    // Resolve session-level peer template from INHERIT_PEER_SESSION.
    {
        auto& inhPolField = configs.get<Config::BgpNeighbor::INHERIT_PEER_POLICY>();
        if (inhPolField.hasValue())
            configs.setPeerPolicyTemplate(parent.getProcess().getNtable().lookupPeerPolicyTemplate(inhPolField.load()));
    }
}

void NeighborAf::updateOrfFilter(const std::vector<OrfPrefixEntry>& entries)
{
    for (const auto& e : entries)
    {
        if (e.action == BGP_ORF_ACTION_REMOVE_ALL)
        {
            orfFilter.clear();
            continue;
        }
        auto it = std::find_if(orfFilter.begin(), orfFilter.end(),
            [&](const OrfPrefixEntry& f) { return f.sequence == e.sequence; });
        if (e.action == BGP_ORF_ACTION_REMOVE)
        {
            if (it != orfFilter.end()) orfFilter.erase(it);
            continue;
        }
        // ADD
        if (it != orfFilter.end())
            *it = e;
        else
            orfFilter.push_back(e);
    }
    std::sort(orfFilter.begin(), orfFilter.end(),
        [](const OrfPrefixEntry& a, const OrfPrefixEntry& b) { return a.sequence < b.sequence; });
}

NeighborAf::~NeighborAf()
{
    parent.getConfigs().get<Config::BgpNeighborSession::AF_NEIGHBOR>().erase(
        family.afi | uint32_t(family.afi << 16));
}
}
