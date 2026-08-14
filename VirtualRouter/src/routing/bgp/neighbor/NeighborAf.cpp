// NeighborAf.cpp

#include <VirtualRouter.h>
#include <algorithm>
#include <chrono>

#include "NeighborAf.h"
#include "Neighbor.h"
#include "bgp/neighbor/NeighborTable.h"
#include "bgp/af/AddressFamilyInstance.h" // IWYU pragma: keep

namespace routing::bgp
{
NeighborAf::NeighborAf(const AfiSafi& fam, AddressFamilyVariant& af, Neighbor& p)
    : family(fam),
      mpNegotiated(false),
      parent(p),
      configs(fam, [&p, &fam]() -> config::BgpNeighborRegistry& {
          auto& afEntry = p.process.getConfigs().get<config::Bgp::ADDRESS_FAMILIES>().get().at(fam.flatten());
          return afEntry->get<config::BgpAddressFamily::NEIGHBOR>().emplaceBack(p.neighborAddress);
      }()),
      af(af)
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

void NeighborAf::enqueueSyncAdditionalPaths()
{
    enqueueMarkAttr(OutAttr::ADD_PATH);
}

void NeighborAf::enqueueSyncDefaultOriginate()
{
    // Sent/withdrawn explicitly (not Loc-RIB derived), so applied immediately.
    parent.scheduler.post([this]() {
        if (!parent.session || !parent.session->established())
            return;
        std::visit([this](auto& fam){
            if (configs.get<config::BgpAfBase::DEFAULT_ORIGINATE>().load())
                fam.sendDefaultOriginate(*parent.session);
            else
                fam.withdrawDefaultOriginate(*parent.session);
        }, af);
    });
}

void NeighborAf::enqueueSyncSlowPeer()
{
    enqueueMarkAttr(OutAttr::NEXT_HOP);
}

void NeighborAf::enqueueSyncActivate()
{
    parent.scheduler.post([this]() {
        std::visit([this](auto& fam) {
            if (configs.get<config::BgpNeighbor::ACTIVATE>().load())
            {
                // Dumping the Loc-RIB only makes sense once the session can carry it.
                if (parent.session && parent.session->established())
                    fam.onPeerEstablished(*parent.session);
            }
            else
            {
                dirtyOut.set(static_cast<size_t>(OutAttr::ACTIVATE));
                fam.markNeighborOutDirty(parent.getRouterId());
            }
        }, af);
    });
}

void NeighborAf::enqueueSyncAdvertiseDiverse()
{
    enqueueMarkAttr(OutAttr::ADD_PATH);
}

void NeighborAf::markAttr(OutAttr attr)
{
    dirtyOut.set(static_cast<size_t>(attr));
    std::visit([this](auto& fam) { fam.markNeighborOutDirty(parent.getRouterId()); }, af);
}

void NeighborAf::markAttrs(OutAttrMask attrs)
{
    dirtyOut |= attrs;
    std::visit([this](auto& fam) { fam.markNeighborOutDirty(parent.getRouterId()); }, af);
}

void NeighborAf::enqueueMarkAttr(OutAttr attr)
{
    parent.scheduler.post([this, attr]() { markAttr(attr); });
}

void NeighborAf::enqueueMarkAttrs(OutAttrMask attrs)
{
    parent.scheduler.post([this, attrs]() { markAttrs(attrs); });
}

void NeighborAf::enqueueMarkInbound(InDirty category)
{
    parent.scheduler.post([this, category]() {
        std::visit([this, category](auto& fam) {
            fam.markInboundDirty(category, parent.getRouterId());
        }, af);
    });
}

void NeighborAf::enqueueConnectionRestart()
{
    parent.enqueueConnectionRestart();
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
    if (!remoteAs.hasValue())
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
    //parent.getConfigs().get<config::BgpNeighborSession::AF_NEIGHBOR>().erase(
        //family.afi | uint32_t(family.afi << 16));
}
} // namespace routing
