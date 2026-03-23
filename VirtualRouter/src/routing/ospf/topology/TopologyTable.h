// OspfTopologyTable.h

#ifndef OSPF_TOPOLOGY_TABLE_H
#define OSPF_TOPOLOGY_TABLE_H

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "TopologyTypes.hpp"

namespace routing::ospf
{
struct SpfResult;
struct OspfNextHop;
struct SpfNode;

class TopologyTable
{
public:
    bool consumeSpfResult(uint32_t area, const SpfResult& result);
    bool updateAreaAsbrs(uint32_t area, const std::vector<OspfRouter>& asbrs);
    bool updateAreaAsbr(uint32_t area, const OspfRouter& asbrs, bool remove = false);

    const RouterReach* lookup(uint32_t rid) const;
    uint32_t lookupDistance(uint32_t rid) const;

    void clear();

private:
    enum class Type { INTRA, INTER };
    struct ReachEntry
    {
        Type type;
        uint32_t rid;

        bool operator==(const ReachEntry& other) const noexcept
        {
            return type == other.type &&
                   rid == other.rid;
        }
    };

    struct ReachEntryHash
    {
        size_t operator()(const ReachEntry& k) const
        {
            uint64_t packed = (static_cast<uint64_t>(k.rid) << 1) | static_cast<uint64_t>(k.type);
            packed += 0x9e3779b97f4a7c15ull;
            packed = (packed ^ (packed >> 30)) * 0xbf58476d1ce4e5b9ull;
            packed = (packed ^ (packed >> 27)) * 0x94d049bb133111ebull;
            packed ^= (packed >> 31);

            return static_cast<size_t>(packed);
        }
    };

    std::unordered_map<uint32_t, std::unordered_set<ReachEntry, ReachEntryHash>> areaReach;
    std::unordered_map<uint32_t, RouterReach> reach;

private:
    std::optional<bool/*ADD*/>/*CHANGE*/ mergeCanidate(const OspfRouter& canidate);
    bool mergeCanidates(uint32_t area, const std::vector<OspfRouter>& canidate, Type type);
};
} // namespace routing

#endif // OSPF_TOPOLOGY_TABLE_H

