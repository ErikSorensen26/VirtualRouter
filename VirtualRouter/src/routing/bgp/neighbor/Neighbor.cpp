// Neighbor.cpp

#include <VirtualRouter.h>

#include "Neighbor.h"
#include "NeighborAf.h"
#include "PeerTemplate.h"
#include "bgp/BgpScope.h"
#include "bgp/af/ScopeAccessor.h"

namespace routing::bgp
{
Neighbor::Neighbor(config::BgpNeighborSessionRegistry& cfgs, const types::IPAddress& ipAddress, NeighborTable& ntable, core::ProcessQueue& schldr)
    : neighborAddress(ipAddress),
      ntable(ntable),
      scheduler(schldr.ref()),
      configs(cfgs)
{
    initialize();

    // Resolve peer group
    if (!getDynamic()) ntable.syncPeerGroup(*this);
}

Neighbor::Neighbor(PeerGroup& dynCfgs, const types::IPAddress& ipAddress, NeighborTable& ntable, core::ProcessQueue& schldr)
    : neighborAddress(ipAddress),
      ntable(ntable),
      scheduler(schldr.ref()),
      configs(dynCfgs)
{
    initialize();
}

Neighbor::~Neighbor()
{
    scheduler.release();
    priv.afNeighbors.clear();
}

void Neighbor::initialize()
{
    configs.getConfigs().context().set(this);
    configs.getConfigs().get<config::BgpNeighborSession::BGP_BASE>().get().context().set(this);

    // Activate AFs
    auto afVrfs = ntable.getConfigs().get<config::Bgp::AF_VRF>();
    if (auto it = afVrfs.find(ntable.scope.routingInstance.getName()); it != afVrfs.end())
    {
        if (auto& ipv4 = it->second->get<config::BgpAfVrf::IPV4_UNICAST>(); ipv4.hasValue())
            addAfNeighbor({BGP_AFI_IPV4, BGP_SAFI_UNICAST});
        if (auto& ipv6 = it->second->get<config::BgpAfVrf::IPV4_UNICAST>(); ipv6.hasValue())
            addAfNeighbor({BGP_AFI_IPV6, BGP_SAFI_UNICAST});
        // Add more later
    }

    // Resolve session-level peer template from INHERIT_PEER_SESSION.
    ntable.syncPeerSessionTemplate(*this);
}

void Neighbor::enqueueConnectionRestart()
{
    scheduler.post([this]() {
        ntable.restartNeighbor(*this);
    });
}

void Neighbor::enqueueSyncShutdown(bool shutdown)
{
    scheduler.post([this, shutdown]() {
        if (shutdown)
            ntable.shutdownNeighbor(*this);
        else
            ntable.unshutdownNeighbor(*this);
    });
}

void Neighbor::enqueueBuildAttributeRanges()
{
    scheduler.post([this]() {
        buildAttributeRanges();
    });
}

void Neighbor::enqueueSyncRemoteAs(std::optional<uint32_t> remoteAs)
{
    scheduler.post([this, remoteAs]() {
        syncEbgp(remoteAs);
        ntable.restartNeighbor(*this);
    });
}

void Neighbor::enqueueMarkAllOutbound(OutAttr attr)
{
    scheduler.post([this, attr]() {
        forEachAfNeighbor([&](NeighborAf& afNbr) { afNbr.markAttr(attr); });
    });
}

void Neighbor::enqueueSyncPeerGroup(std::optional<std::string> name)
{
    scheduler.post([this, name = std::move(name)]() {
        auto* pg = name ? ntable.lookupPeerGroup(*name) : nullptr;
        configs.setPeerGroup(pg);
        forEachAfNeighbor([pg](NeighborAf& afNbr) { afNbr.setPeerGroupSync(pg); });
    });
}

void Neighbor::enqueueSyncPeerSessionTemplate(std::optional<std::string> name)
{
    scheduler.post([this, name = std::move(name)]() {
        ntable.syncPeerSessionTemplate(*this);
    });
}

void Neighbor::addAfNeighbor(const AfiSafi& afi)
{
    AddressFamilyVariant* af = ntable.findAddressFamily(afi);
    assert(af);
    config::BgpNeighborRegistry* parentCfgs = configs.getConfigs().resolveParent<config::BgpNeighborRegistry>();
    assert(parentCfgs);
    priv.afNeighbors.try_emplace(afi, *parentCfgs, afi, *af, *this);
}

void Neighbor::delAfNeighbor(AfiSafi& afi)
{
    priv.afNeighbors.erase(afi);
}

NeighborAf& Neighbor::getAfNeighbor(const AfiSafi& afi)
{
    auto it = priv.afNeighbors.find(afi);
    assert(it != priv.afNeighbors.end());
    return it->second;
}

NeighborAf* Neighbor::findAfNeighbor(const AfiSafi& afi)
{
    auto it = priv.afNeighbors.find(afi);
    return it != priv.afNeighbors.end() ? &it->second : nullptr;
}

void Neighbor::syncEbgp()
{
    auto remAs = configs.get<config::BgpNeighborSession::REMOTE_AS>();
    syncEbgp(remAs.hasValue() ? std::optional{remAs.load()} : std::nullopt);
}

BgpScope& Neighbor::getScope() const noexcept
{
    return ntable.scope;
}

void Neighbor::syncEbgp(std::optional<uint32_t> remoteAs)
{
    if (!remoteAs)
    {
        priv.isEbgp.store(false, std::memory_order_release);
        priv.inConfed.store(false, std::memory_order_release);
        return;
    }
    const bool inConfed = ntable.isPeerConfed(*remoteAs);
    priv.inConfed.store(inConfed, std::memory_order_release);
    priv.isEbgp.store(*remoteAs != ScopeAccessor::getAsNum(ntable.scope) && !inConfed,
                      std::memory_order_release);
}

bool Neighbor::isEbgp() const noexcept
{
    return priv.isEbgp.load(std::memory_order_relaxed);
}

bool Neighbor::isConfedEbgp() const noexcept
{
    return priv.inConfed.load(std::memory_order_relaxed);
}

const PeerGroup* Neighbor::getDynamic() const noexcept
{
    return configs.getDynamicGroup();
}

void Neighbor::buildAttributeRanges()
{
    attrRanges.discard.reset();
    attrRanges.withdraw.reset();

    configs.get<config::BgpNeighborSession::PATH_ATTRIBUTE_DISCARD>().readEach(
        [this](const config::BgpPathAttribute& range)
        {
            for (uint16_t i = range.start(); i <= range.end(); ++i)
                attrRanges.discard.set(i);
        }
    );

    configs.get<config::BgpNeighborSession::PATH_ATTRIBUTE_TREAT_AS_WITHDRAW>().readEach(
        [this](const config::BgpPathAttribute& range)
        {
            for (uint16_t i = range.start(); i <= range.end(); ++i)
                attrRanges.withdraw.set(i);
        }
    );
}

void Neighbor::unshutdown()
{
    ntable.unshutdownNeighbor(*this);
}
} // namespace routing
