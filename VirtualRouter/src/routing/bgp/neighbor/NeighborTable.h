// BgpNeighborTable.h

#ifndef BGP_NEIGHBOR_TABLE_H
#define BGP_NEIGHBOR_TABLE_H

#include <unordered_map>
#include <cstdint>

#include "bgp/neighbor/Neighbor.h"
#include "bgp/neighbor/PeerTemplate.h"

namespace types { struct IPAddress; }

namespace routing::bgp
{
class BgpProcess;
class Neighbor;

class NeighborTable
{
public:
    NeighborTable(BgpProcess& proc);

    void syncNeighbors();

    void syncPeerGroups();

    Neighbor* createNeighbor(const types::IPAddress& ipAddress);

    void deleteNeighbor(const types::IPAddress& ipAddress);

    // Create a passive-only neighbor from a bgp listen range match.
    // The neighbor inherits all config from the named peer-group.
    Neighbor* createDynamicNeighbor(const types::IPAddress& ipAddress, const std::string& peerGroupName);

    Neighbor* lookup(const types::IPAddress& ipAddress);
    const Neighbor* lookup(const types::IPAddress& ipAddress) const;

    Neighbor* lookup(uint32_t rid);
    const Neighbor* lookup(uint32_t rid) const;

    bool activatePeer(const types::IPAddress& nbr, uint32_t peer);
    bool deactivatePeer(uint32_t peer);

    void cancelAllHoldTimers();

    // Re-evaluate disableConnectionCheck from all neighbor configs.
    void runDccCheck();

    // Whether any neighbor has DISABLE_CONNECTION_CHECK enabled.
    bool disableConnectionCheck = false;

    PeerGroup& createPeerGroup(const std::string& name);
    void removePeerGroup(const std::string& name);
    PeerSessionTemplate& createPeerSessionTemplate(const std::string& name);
    void removePeerSessionTemplate(const std::string& name);
    PeerPolicyTemplate& createPeerPolicyTemplate(const std::string& name);
    void removePeerPolicyTemplate(const std::string& name);

    PeerGroup* lookupPeerGroup(const std::string& name);
    const PeerGroup* lookupPeerGroup(const std::string& name) const;
    PeerSessionTemplate* lookupPeerSessionTemplate(const std::string& name);
    const PeerSessionTemplate* lookupPeerSessionTemplate(const std::string& name) const;
    PeerPolicyTemplate* lookupPeerPolicyTemplate(const std::string& name);
    const PeerPolicyTemplate* lookupPeerPolicyTemplate(const std::string& name) const;

    template <typename F>
    void forEachNeighbor(F&& fn)
    {
        for (auto& [addr, nbr] : neighbors)
            fn(nbr);
    }

    template <typename F>
    void forEachNeighbor(F&& fn) const
    {
        for (const auto& [addr, nbr] : neighbors)
            fn(nbr);
    }

    template <typename F>
    void forEachPeer(F&& fn)
    {
        for (auto& [addr, nbr] : peers)
            fn(*nbr);
    }

    template <typename F>
    void forEachPeer(F&& fn) const
    {
        for (const auto& [addr, nbr] : peers)
            fn(*nbr);
    }

private:
    std::unordered_map<types::IPAddress, Neighbor> neighbors;
    std::unordered_map<uint32_t, Neighbor*> peers;

    BgpProcess& process;
    PeerTemplateTable peerTemplates;
};
} // namespace routing

#endif // BGP_NEIGHBOR_TABLE_H

