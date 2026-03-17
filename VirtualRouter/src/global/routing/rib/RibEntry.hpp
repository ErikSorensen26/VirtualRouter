// RibEntry.hpp

#ifndef RIB_ENTRY_HPP
#define RIB_ENTRY_HPP

#include "RouteSource.hpp"
#include <IPAddress.hpp>
#include <optional>

#define MAX_NEXTHOP 8

template <typename AddrType>
struct PrefixKey
{
    AddrType prefix;
    uint8_t  length;

    bool operator==(const PrefixKey& o) const noexcept
    {
        return prefix == o.prefix && length == o.length;
    }
};

template <typename AddrType>
struct PrefixHash
{
    size_t operator()(const PrefixKey<AddrType>& k) const noexcept
    {
        uint64_t h1 = std::hash<AddrType>{}(k.prefix);
        uint64_t h2 = k.length;
        uint64_t hash = h1;
        hash ^= h2 + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        return static_cast<size_t>(hash);
    }
};

template <typename AddrType>
struct NextHopPath
{
    std::optional<AddrType> nextHop;
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
    uint64_t processId;
    uint8_t adminDistance;
    uint64_t metric;
    uint32_t tag = 0;
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
        {
            auto& nextHop = nextHops[i];
            if (!nextHop.nextHop.has_value() && nextHop.iface == iface)
            {
                nextHop.nextHop = nhAddr;
                return true;
            }
            else if (nextHop.nextHop.has_value() && nextHop.nextHop.value() == nhAddr && nextHop.iface == iface)
            {
                return false;
            }
        }

        nextHops[nextHopCount++] = { nhAddr, iface, weight };
        return true;
    }

    bool addNextHopInterface(uint32_t iface, uint32_t weight = 1) noexcept
    {
        if (nextHopCount >= MAX_NEXTHOP)
            return false;

        for (uint8_t i = 0; i < nextHopCount; ++i)
            if (nextHops[i].iface == iface)
                return false;
        nextHops[nextHopCount++] = { std::nullopt, iface, weight };
        return true;
    }

    RibEntry(const RibEntry<AddrType>& other) noexcept
        : prefix(other.prefix),
          length(other.length),
          source(other.source),
          processId(other.processId),
          adminDistance(other.adminDistance),
          metric(other.metric),
          tag(other.tag),
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
        adminDistance = other.adminDistance;
        metric = other.metric;
        tag = other.tag;
        topInfo = other.topInfo;

        nextHopCount = other.nextHopCount;
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];

        return *this;
    }
};

template <typename AddrType>
struct FibEntry
{
    static_assert(std::is_unsigned_v<AddrType>, "AddrType must be unsigned integral");

    AddrType prefix;
    uint8_t length;
    NextHopPath<AddrType> nextHops[MAX_NEXTHOP];
    uint8_t nextHopCount = 0;

    FibEntry() = default;

    void clear() noexcept { nextHopCount = 0; }

    bool empty() const noexcept { return nextHopCount == 0; }

    bool addNextHop(const AddrType nhAddr, uint32_t iface, uint32_t weight = 1) noexcept
    {
        if (nextHopCount >= MAX_NEXTHOP)
            return false;

        for (uint8_t i = 0; i < nextHopCount; ++i)
        {
            auto& nextHop = nextHops[i];
            if (!nextHop.nextHop.has_value() && nextHop.iface == iface)
            {
                nextHop.nextHop = nhAddr;
                return true;
            }
            else if (nextHop.nextHop.has_value() && nextHop.nextHop.value() == nhAddr && nextHop.iface == iface)
            {
                return false;
            }
        }

        nextHops[nextHopCount++] = { nhAddr, iface, weight };
        return true;
    }

    bool addNextHopInterface(uint32_t iface, uint32_t weight = 1) noexcept
    {
        if (nextHopCount >= MAX_NEXTHOP)
            return false;

        for (uint8_t i = 0; i < nextHopCount; ++i)
            if (nextHops[i].iface == iface)
                return false;
        nextHops[nextHopCount++] = { std::nullopt, iface, weight };
        return true;
    }

    FibEntry(const FibEntry<AddrType>& other) noexcept
        : prefix(other.prefix),
          length(other.length),
          nextHopCount(other.nextHopCount)
    {
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];
    }

    FibEntry(const RibEntry<AddrType>& other) noexcept
        : prefix(other.prefix),
          length(other.length),
          nextHopCount(other.nextHopCount)
    {
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];
    }

    FibEntry& operator=(const FibEntry<AddrType>& other) noexcept
    {
        if (this == &other)
            return *this;

        prefix = other.prefix;
        length = other.length;

        nextHopCount = other.nextHopCount;
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];

        return *this;
    }

    FibEntry& operator=(const RibEntry<AddrType>& other) noexcept
    {
        prefix = other.prefix;
        length = other.length;

        nextHopCount = other.nextHopCount;
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];

        return *this;
    }
};

#endif // RIB_ENTRY_HPP
