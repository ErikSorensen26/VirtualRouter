// Neighbor.cpp

#include <VirtualRouter.h>

#include "configs/Registry.hpp"
#include "Neighbor.h"
#include "NeighborAf.h"
#include "PeerTemplate.h"
#include "bgp/BgpProcess.h"

namespace BGP
{
Neighbor::Neighbor(const IPAddress& ipAddress, BgpProcess& proc)
    : neighborAddress(ipAddress),
      process(proc),
      scheduler(proc.getScheduler()),
      configs([&proc, &ipAddress]() {
          auto& procConfigs = proc.getConfigs();
          auto& neighborConfigs = procConfigs.get<Config::Bgp::NEIGHBOR>();
          auto key = Config::generateBgpSessionKey(proc.routingInstance->getInstanceId(), ipAddress);
          return proc.routingInstance->getRegistry().emplaceBack(neighborConfigs, ipAddress, key);
      }())
{
    configs.getConfigs()->context().set(this);

    // Resolve peer group
    {
        auto& pgField = configs.get<Config::BgpNeighborSession::PEER_GROUP>();
        if (pgField.hasValue())
            configs.setPeerGroup(proc.getNtable().lookupPeerGroup(pgField.load()));
    }

    // Resolve session-level peer template from INHERIT_PEER_SESSION.
    {
        auto& inhSessField = configs.get<Config::BgpNeighborSession::INHERIT_PEER_SESSION>();
        if (inhSessField.hasValue())
            configs.setPeerSessionTemplate(proc.getNtable().lookupPeerSessionTemplate(inhSessField.load()));
    }
}

Neighbor::~Neighbor()
{
    process.getConfigs().get<Config::Bgp::NEIGHBOR>().erase(neighborAddress);
}

void Neighbor::addAfNeighbor(AfiSafi& afi)
{
    afNeighbors.emplace(afi, afi, *this);
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
    auto& remAs = configs.get<Config::BgpNeighborSession::REMOTE_AS>();
    if (!remAs.hasValue()) return false;
    return remAs.load() != process.asNumber;
}

void Neighbor::buildAttributeRanges()
{
    attrRanges.discard.reset();
    attrRanges.withdraw.reset();

    configs.get<Config::BgpNeighborSession::PATH_ATTRIBUTE>().withRead([this](std::vector<std::tuple<bool, uint8_t, uint8_t>>& ranges) {
        for (const auto& [disc, lo, hi] : ranges)
        {
            if (disc)
            {
                for (uint16_t i = lo; i <= hi; ++i)
                    attrRanges.discard.set(i);
            }
            else
            {
                for(uint16_t i = lo; i <= hi; ++i)
                    attrRanges.withdraw.set(i);
            }
        }
    });
}
}
