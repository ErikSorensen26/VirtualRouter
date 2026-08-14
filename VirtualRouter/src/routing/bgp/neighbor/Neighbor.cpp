// Neighbor.cpp

#include <VirtualRouter.h>

#include "Neighbor.h"
#include "NeighborAf.h"
#include "PeerTemplate.h"
#include "bgp/BgpProcess.h"

namespace routing::bgp
{
Neighbor::Neighbor(const types::IPAddress& ipAddress, NeighborTable& ntable, core::ProcessQueue& schldr)
    : neighborAddress(ipAddress),
      ntable(ntable),
      scheduler(schldr.ref()),
      configs(ntable.ensureNeighborConfigs(ipAddress))
{
    configs.getConfigs().context().set(this);

    // Resolve peer group
    {
        auto pgField = configs.get<config::BgpNeighborSession::PEER_GROUP>();
        if (pgField.hasValue())
            configs.setPeerGroup(ntable.lookupPeerGroup(pgField.load()));
    }

    // Resolve session-level peer template from INHERIT_PEER_SESSION.
    {
        auto inhSessField = configs.get<config::BgpNeighborSession::INHERIT_PEER_SESSION>();
        if (inhSessField.hasValue())
            configs.setPeerSessionTemplate(ntable.lookupPeerSessionTemplate(inhSessField.load()));
    }
}

Neighbor::~Neighbor()
{
    scheduler.release();
    priv.afNeighbors.clear();
    ntable.removeNeighborConfigs(neighborAddress);
}

void Neighbor::enqueueConnectionRestart()
{
    scheduler.post([this]() {
        ntable.restartNeighbor(*this);
    });
}

void Neighbor::enqueueSyncShutdown()
{
    scheduler.post([this]() {
        if (configs.get<config::BgpNeighborSession::SHUTDOWN>().load())
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

void Neighbor::enqueueSyncRemoteAs()
{
    scheduler.post([this]() {
        syncEbgp();
        ntable.restartNeighbor(*this);
    });
}

void Neighbor::enqueueMarkAllOutbound(OutAttr attr)
{
    scheduler.post([this, attr]() {
        forEachAfNeighbor([&](NeighborAf& afNbr) { afNbr.markAttr(attr); });
    });
}

void Neighbor::addAfNeighbor(AfiSafi& afi)
{
    AddressFamilyVariant* af = ntable.findAddressFamily(afi);
    assert(af);
    priv.afNeighbors.try_emplace(afi, afi, *af, *this);
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
    if (!remAs.hasValue())
    {
        priv.isEbgp.store(false, std::memory_order_release);
        priv.inConfed.store(false, std::memory_order_release);
        return;
    }
    const bool inConfed = ntable.isPeerConfed(remAs.load());
    priv.inConfed.store(inConfed, std::memory_order_release);
    priv.isEbgp.store(remAs.load() != ntable.process.asNumber && !inConfed,
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

void Neighbor::buildAttributeRanges()
{
    attrRanges.discard.reset();
    attrRanges.withdraw.reset();

    configs.get<config::BgpNeighborSession::PATH_ATTRIBUTE_DISCARD>().withRead([this](const auto& rangesList) {
        for (const auto& [lo, hi] : rangesList)
        {
            for (uint16_t i = lo; i <= hi; ++i)
                attrRanges.discard.set(i);
        }
    });
    configs.get<config::BgpNeighborSession::PATH_ATTRIBUTE_TREAT_AS_WITHDRAW>().withRead([this](const auto& rangesList) {
        for (const auto& [lo, hi] : rangesList)
        {
            for (uint16_t i = lo; i <= hi; ++i)
                attrRanges.withdraw.set(i);
        }
    });
}

void Neighbor::unshutdown()
{
    ntable.unshutdownNeighbor(*this);
}
} // namespace routing
