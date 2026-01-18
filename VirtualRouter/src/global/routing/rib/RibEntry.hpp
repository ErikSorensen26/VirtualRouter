// RibEntry.hpp

#ifndef RIB_ENTRY_HPP
#define RIB_ENTRY_HPP

#include "RouteSource.hpp"
#include <IPAddress.hpp>

#define MAX_NEXTHOP 8

template <typename AddrType>
struct NextHopPath
{
    AddrType nextHop;
    uint32_t iface;
    uint32_t weight;
};

template <typename AddrType>
struct RibEntry
{
    static_assert(std::is_unsigned_v<AddrType>, "AddrType must be unsigned integral");

    AddrType prefix;
    uint8_t length;
    RouteSource source;
    uint32_t processId;
    uint8_t topoId;
    uint8_t adminDistance;
    uint64_t metric;
    void* topInfo = nullptr;

    NextHopPath<AddrType> nextHops[MAX_NEXTHOP];
    uint8_t nextHopCount = 0;

    RibEntry() = default;

    void clear() noexcept { nextHopCount = 0; }

    bool empty() const noexcept { return nextHopCount == 0; }

    bool addNextHop(const AddrType nhAddr, uint32_t iface, uint32_t weight = 1) noexcept
    {
        if (nextHopCount >= MAX_NEXTHOP)
            return false;

        for (uint8_t i = 0; i < nextHopCount; ++i)
            if (nextHops[i].nextHop == nhAddr && nextHops[i].iface == iface)
                return false;
        nextHops[nextHopCount++] = { nhAddr, iface, weight };
        return true;
    }

    RibEntry(const RibEntry<AddrType>& other) noexcept
        : prefix(other.prefix),
          length(other.length),
          source(other.source),
          processId(other.processId),
          topoId(other.topoId),
          adminDistance(other.adminDistance),
          metric(other.metric),
          topInfo(other.topInfo),
          nextHopCount(other.nextHopCount)
    {
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];
    }

    RibEntry& operator=(const RibEntry<AddrType>& other) noexcept
    {
        if (this == &other)
            return *this;

        prefix = other.prefix;
        length = other.length;
        source = other.source;
        processId = other.processId;
        topoId = other.topoId;
        adminDistance = other.adminDistance;
        metric = other.metric;
        topInfo = other.topInfo;

        nextHopCount = other.nextHopCount;
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];

        return *this;
    }
};

#endif // RIB_ENTRY_HPP
