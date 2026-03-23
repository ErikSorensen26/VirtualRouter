// OspfNeighborTable.h

#ifndef OSPF_NEIGHBOR_TABLE_H
#define OSPF_NEIGHBOR_TABLE_H

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace types { struct IPAddress; }

namespace routing::ospf
{
class OspfInterface;
class Neighbor;
class NeighborTable
{
public:
    NeighborTable(OspfInterface& iface);

    void syncUnicast();
    void clearUnicast();

    Neighbor* createNeighbor(uint32_t rid, const types::IPAddress& ipAddress, bool unicast = false);
    void deleteNeighbor(uint32_t rid, bool unicast);
    Neighbor* lookup(uint32_t rid);
    const Neighbor* lookup(uint32_t rid) const;

    void cancelAllInactiveTimers();

    std::optional<size_t> addNeighborList(uint8_t* buf, size_t maxSize);
    std::unordered_map<uint32_t, Neighbor> neighbors;

    struct UnicastConfigs
    {
        std::optional<uint16_t> cost{std::nullopt};
        bool databaseFilter{false};
        uint16_t pollInterval{120};
        uint8_t priority{0};
    };

private:
    std::unordered_map<types::IPAddress, UnicastConfigs> unicast;
    OspfInterface& iface;
};
} // namespace routing

#endif // OSPF_NEIGHBOR_TABLE_H
