// BgpNeighbor.h

#ifndef BGP_NEIGHBOR_H
#define BGP_NEIGHBOR_H

#include <bitset>
#include <ControlScheduler.h>

#include "bgp/BgpTypes.hpp"
#include "NeighborConfigs.hpp"
#include "NeighborAf.h"

namespace routing::bgp
{
class BgpProcess;
class Session;
class NeighborAf;

class Neighbor
{
public:
    Neighbor(const types::IPAddress& ipAddress, BgpProcess& proc);
    ~Neighbor();

    const types::IPAddress neighborAddress;

    uint32_t rid = 0;

    Session* session = nullptr;

    // True for neighbors created dynamically via bgp listen range.
    // Dynamic neighbors are passive-only and not owned by the static config.
    bool dynamic = false;

    BgpProcess& getProcess() { return process; }
    const BgpProcess& getProcess() const { return process; }

    bool isEbgp() const noexcept;
    bool isConfedEbgp() const noexcept;

    void addAfNeighbor(AfiSafi& afi);
    void delAfNeighbor(AfiSafi& afi);
    NeighborAf& getAfNeighbor(const AfiSafi& afi);
    const NeighborAf& getAfNeighbor(const AfiSafi& afi) const;

    template <typename F>
    void forEachAfNeighbor(F&& fn)
    {
        for (auto& [_, nbr] : afNeighbors)
            fn(nbr);
    }

    template <typename F>
    void forEachAfNeighbor(F&& fn) const
    {
        for (const auto& [_, nbr] : afNeighbors)
            fn(nbr);
    }

    NeighborConfigs& getConfigs() { return configs; }
    const NeighborConfigs& getConfigs() const { return configs; }
    core::ProcessQueueRef& getScheduler() { return scheduler; }
    const core::ProcessQueueRef& getScheduler() const { return scheduler; }

    // Attribute ranges
    struct AttributeRanges
    {
        std::bitset<256> discard;
        std::bitset<256> withdraw;
    };

    void buildAttributeRanges();
    const AttributeRanges& getAttrRanges() { return attrRanges; }

private:
    AttributeRanges attrRanges;

private:
    friend NeighborAf;

    BgpProcess& process;
    core::ProcessQueueRef scheduler;

    std::unordered_map<AfiSafi, NeighborAf> afNeighbors;

    NeighborConfigs configs;
};
} // namespace routing

#endif // BGP_NEIGHBOR_H

