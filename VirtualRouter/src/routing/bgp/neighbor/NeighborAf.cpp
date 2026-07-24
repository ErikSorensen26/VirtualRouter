// NeighborAf.cpp

#include <VirtualRouter.h>
#include <algorithm>
#include <chrono>

#include "NeighborAf.h"
#include "Neighbor.h"
#include "bgp/neighbor/NeighborTable.h"

namespace routing::bgp
{
NeighborAf::NeighborAf(const AfiSafi& fam, Neighbor& p)
    : family(fam),
      mpNegotiated(false),
      parent(p),
      configs(fam, [&p, &fam]() -> config::BgpNeighborRegistry& {
          auto neighborConfigs = p.configs.get<config::BgpNeighborSession::AF_NEIGHBOR>();
          uint32_t id = fam.afi | uint32_t(fam.afi) << 16;
          return neighborConfigs.emplaceBack(id);
      }())
{
    configs.getConfigs().context().set(this);

    // Resolve peer group
    {
        auto pgField = parent.configs.get<config::BgpNeighborSession::PEER_GROUP>();
        if (pgField.hasValue())
            configs.setPeerGroup(parent.ntable.lookupPeerGroup(pgField.load()));
    }

    // Resolve session-level peer template from INHERIT_PEER_SESSION.
    {
        auto inhPolField = configs.get<config::BgpNeighbor::INHERIT_PEER_POLICY>();
        if (inhPolField.hasValue())
            configs.setPeerPolicyTemplate(parent.ntable.lookupPeerPolicyTemplate(inhPolField.load()));
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

void NeighborAf::invalidate()
{
    orfFilter.clear();
    maxPfxWarned = false;
    cancelPfxRestart();
    isSlowPeer = false;
    slowFirstSeen = {};
}

void NeighborAf::schedulePfxRestart(uint16_t minutes)
{
    cancelPfxRestart();
    auto expiry = std::chrono::steady_clock::now() + std::chrono::minutes(minutes);
    priv.maxPfxRestartTimerId = parent.scheduler.postAfter(expiry, [this](uint32_t) {
        priv.maxPfxRestartTimerId = 0;
        parent.unshutdown();
    });
}

void NeighborAf::cancelPfxRestart()
{
    if (priv.maxPfxRestartTimerId != 0)
    {
        parent.scheduler.cancel(priv.maxPfxRestartTimerId);
        priv.maxPfxRestartTimerId = 0;
    }
}

Session* NeighborAf::getSession() noexcept
{
    return parent.session;
}

std::optional<uint32_t> NeighborAf::getRemoteAs() const noexcept
{
    auto remoteAs = parent.configs.get<config::BgpNeighborSession::REMOTE_AS>();
    if (remoteAs.hasValue())
        return std::nullopt;
    return remoteAs.load();
}

bool NeighborAf::isEbgp() const noexcept
{
    return parent.isEbgp();
}

bool NeighborAf::isConfedEbgp() const noexcept
{
    return parent.isConfedEbgp();
}

NeighborAf::~NeighborAf()
{
    cancelPfxRestart();
    parent.configs.get<config::BgpNeighborSession::AF_NEIGHBOR>().erase(
        family.afi | uint32_t(family.afi << 16));
}
} // namespace routing
