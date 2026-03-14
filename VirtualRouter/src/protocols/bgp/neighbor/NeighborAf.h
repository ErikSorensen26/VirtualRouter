// NeighborAf.h

#ifndef BGP_NEIGHBOR_AF_H
#define BGP_NEIGHBOR_AF_H

#include "bgp/BgpTypes.hpp"
#include "bgp/rib/RibTypes.hpp"
#include "NeighborAfConfigs.hpp"
#include "bgp/af/AddressFamily.hpp"

namespace BGP
{
class Neighbor;
class BgpProcess;
class PeerGroup;
class PeerPolicyTemplate;
class Session;

class NeighborAf
{
public:
    NeighborAf(const AfiSafi& family, Neighbor& parent);
    ~NeighborAf();

    const AfiSafi family;

    NeighborAfConfigs& getConfigs() { return configs; }
    const NeighborAfConfigs& getConfigs() const { return configs; }
    Neighbor& globalNbr() { return parent; }
    const Neighbor& globalNbr() const { return parent; }

    AddressFamilyVariant& getAddressFamily();

    bool mpNegotiated;

    // ORF filter received FROM this peer — applied to our Adj-RIB-Out. Cleared on session reset.
    std::vector<OrfPrefixEntry> orfFilter;
    void updateOrfFilter(const std::vector<OrfPrefixEntry>& entries);

    // ORF filter we advertise TO this peer — set from inbound prefix-list config.
    std::vector<OrfPrefixEntry> orfOutbound;

    // Maximum-prefix tracking. Reset on session reset.
    bool maxPfxWarned = false;
    void schedulePfxRestart(uint16_t minutes);
    void cancelPfxRestart();

    // Slow peer tracking. Reset on session reset.
    bool isSlowPeer = false;
    std::chrono::steady_clock::time_point slowFirstSeen{};

private:
    uint32_t maxPfxRestartTimerId = 0;

    Neighbor& parent;
    NeighborAfConfigs configs;
};
}

#endif // BGP_NEIGHBOR_AF_H
