// Neighbor.cpp

#include <VirtualRouter.h>

#include "Neighbor.h"
#include "NeighborAf.h"
#include "PeerTemplate.h"
#include "bgp/BgpProcess.h"

namespace routing::bgp
{
Neighbor::Neighbor(const types::IPAddress& ipAddress, BgpProcess& proc)
    : neighborAddress(ipAddress),
      process(proc),
      scheduler(proc.getSchedulerQueue().ref()),
      configs([&proc, &ipAddress]() -> config::BgpNeighborSessionRegistry& {
          auto& procConfigs = proc.getConfigs();
          auto neighborConfigs = procConfigs.get<config::Bgp::NEIGHBOR>();
          return neighborConfigs.emplaceBack(ipAddress);
      }())
{
    configs.getConfigs().context().set(this);

    // Resolve peer group
    {
        auto pgField = configs.get<config::BgpNeighborSession::PEER_GROUP>();
        if (pgField.hasValue())
            configs.setPeerGroup(proc.getNtable().lookupPeerGroup(pgField.load()));
    }

    // Resolve session-level peer template from INHERIT_PEER_SESSION.
    {
        auto inhSessField = configs.get<config::BgpNeighborSession::INHERIT_PEER_SESSION>();
        if (inhSessField.hasValue())
            configs.setPeerSessionTemplate(proc.getNtable().lookupPeerSessionTemplate(inhSessField.load()));
    }
}

Neighbor::~Neighbor()
{
    process.getConfigs().get<config::Bgp::NEIGHBOR>().erase(neighborAddress);
}

void Neighbor::addAfNeighbor(AfiSafi& afi)
{
    afNeighbors.try_emplace(afi, afi, *this);
}

void Neighbor::delAfNeighbor(AfiSafi& afi)
{
    afNeighbors.erase(afi);
}

NeighborAf& Neighbor::getAfNeighbor(const AfiSafi& afi)
{
    auto it = afNeighbors.find(afi);
    assert(it != afNeighbors.end());
    return  it->second;
}

const NeighborAf& Neighbor::getAfNeighbor(const AfiSafi& afi) const
{
    auto it = afNeighbors.find(afi);
    assert(it != afNeighbors.end());
    return  it->second;
}

bool Neighbor::isEbgp() const noexcept
{
    auto remAs = configs.get<config::BgpNeighborSession::REMOTE_AS>();
    if (!remAs.hasValue()) return false;
    uint32_t peerAs = remAs.load();
    if (peerAs == process.asNumber) return false;

    bool inConfed = false;
    process.getConfigs().get<config::Bgp::BGP_CONFEDERATION_PEERS>().withRead(
        [&](const auto& peersList) {
            for (uint32_t p : peersList)
                    if (p == peerAs) { inConfed = true; return; }
        });
    return !inConfed;
}

bool Neighbor::isConfedEbgp() const noexcept
{
    auto remAs = configs.get<config::BgpNeighborSession::REMOTE_AS>();
    if (!remAs.hasValue()) return false;
    uint32_t peerAs = remAs.load();
    if (peerAs == process.asNumber) return false;

    bool inConfed = false;
    process.getConfigs().get<config::Bgp::BGP_CONFEDERATION_PEERS>().withRead(
        [&](const auto& peersList) {
            for (uint32_t p : peersList)
                    if (p == peerAs) { inConfed = true; return; }
        });
    return inConfed;
}

void Neighbor::buildAttributeRanges()
{
    attrRanges.discard.reset();
    attrRanges.withdraw.reset();

    configs.get<config::BgpNeighborSession::PATH_ATTRIBUTE>().withRead([this](const auto& rangesList) {
        for (const auto& [disc, lo, hi] : rangesList)
        {
            if (disc)
            {
                for (uint16_t i = lo; i <= hi; ++i)
                    attrRanges.discard.set(i);
            }
            else
            {
                for (uint16_t i = lo; i <= hi; ++i)
                    attrRanges.withdraw.set(i);
            }
        }
    });
}
} // namespace routing
