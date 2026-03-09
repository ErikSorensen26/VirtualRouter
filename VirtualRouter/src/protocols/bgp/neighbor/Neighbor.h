// BgpNeighbor.h

#ifndef BGP_NEIGHBOR_H
#define BGP_NEIGHBOR_H

#include <bitset>
#include <ControlScheduler.h>

#include "configs/registry/router/BgpRegistry.h"
#include "bgp/BgpTypes.hpp"

namespace BGP
{
class BgpProcess;
class Session;
class NeighborAf;

class Neighbor
{
public:
    Neighbor(const IPAddress& ipAddress, BgpProcess& proc);
    ~Neighbor();

    const IPAddress neighborAddress;

    uint32_t rid = 0;

    Session* session = nullptr;
    BgpProcess& getProcess() { return process; }
    const BgpProcess& getProcess() const { return process; }

    bool isEbgp() const noexcept;

    void addAfNeighbor(AfiSafi& afi);
    void delAfNeighbor(AfiSafi& afi);
    NeighborAf& getAfNeighbor(AfiSafi& afi);
    const NeighborAf& getAfNeighbor(AfiSafi& afi) const;

    Config::BgpNeighborSessionRegistry& getConfigs() { return configs.get(); }
    const Config::BgpNeighborSessionRegistry& getConfigs() const { return configs.get(); }
    ProcessQueueRef& getScheduler() { return scheduler; }
    const ProcessQueueRef& getScheduler() const { return scheduler; }

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
    ProcessQueueRef scheduler;

    std::unordered_map<AfiSafi, NeighborAf> afNeighbors;

    Config::Reference<Config::BgpNeighborSessionRegistry> configs;
};
}

#endif // BGP_NEIGHBOR_H
