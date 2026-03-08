// RibTypes.hpp

#ifndef BGP_RIB_TYPES_HPP
#define BGP_RIB_TYPES_HPP

#include <cstdint>
#include <vector>
#include <chrono>
#include <limits>
#include <unordered_map>

#include <IPAddress.hpp>

#include "bgp/attributes/AttributeTypes.hpp"
#include "bgp/attributes/AttributeManager.hpp"

namespace BGP
{
class NeighborAf;

template <typename N>
struct BuildUpdate
{
    struct Announcement
    {
        PathAttribute& attrs;
        std::vector<N> nlri;
    };

    std::vector<N> withdrawn;
    std::vector<Announcement> announcements;
};

template <typename N>
struct ParsedUpdate
{
    std::vector<N> withdrawn;
    std::vector<N> announcements;
    std::optional<PathAttribute> attrs;
};

struct RouteBase
{
    RouteBase() = default;

    RouteBase(AttributeManager& mgr, uint32_t id)
        : pathId(id), attrMgr(&mgr)
    {
        attrMgr->retain(id);
    }

    RouteBase(const RouteBase& other)
        : pathId(other.pathId),
          attrMgr(other.attrMgr)
    {
        retainPathRef();
    }

    RouteBase(RouteBase&& other) noexcept
        : pathId(other.pathId),
          attrMgr(other.attrMgr)
    {
        other.pathId = std::nullopt;
        other.attrMgr = nullptr;
    }

    RouteBase& operator=(const RouteBase& other)
    {
        if (this == &other)
            return *this;

        releasePathRef();

        pathId = other.pathId;
        attrMgr = other.attrMgr;

        retainPathRef();
        return *this;
    }

    RouteBase& operator=(RouteBase&& other) noexcept
    {
        if (this == &other)
            return *this;

        releasePathRef();

        pathId = other.pathId;
        attrMgr = other.attrMgr;

        other.pathId = std::nullopt;
        other.attrMgr = nullptr;

        return *this;
    }

    ~RouteBase()
    {
        releasePathRef();
    }

    std::optional<PathAttribute> getPathAttributes() const
    {
        if (attrMgr && pathId)
            return attrMgr->get(*pathId);
        return std::nullopt;
    }

    std::optional<uint32_t> pathId{};

protected:
    void retainPathRef()
    {
        if (attrMgr && pathId)
            attrMgr->retain(*pathId);
    }

    void releasePathRef()
    {
        if (attrMgr && pathId)
            attrMgr->release(*pathId);

        pathId = std::nullopt;
    }

private:
    AttributeManager* attrMgr = nullptr;
};

struct InboundRouteBase : RouteBase
{
    InboundRouteBase(NeighborAf& nbr)
        : sourceNeighbor(nbr) {}

    NeighborAf& sourceNeighbor;
    uint32_t peerAs = 0;
    bool ebgp = true;
    uint64_t igpCost = std::numeric_limits<uint64_t>::max();

    std::chrono::steady_clock::time_point receivedTime =
        std::chrono::steady_clock::now();
};

template <typename N>
struct InboundRoute : InboundRouteBase
{
    InboundRoute(NeighborAf& nbr) : InboundRouteBase(nbr) {}

    N nlri;

    InboundRoute(const InboundRoute&) = delete;
    InboundRoute& operator=(const InboundRoute&) = delete;

    InboundRoute(InboundRoute&&) noexcept = default;
    InboundRoute& operator=(InboundRoute&&) noexcept = default;

    bool operator==(const InboundRoute& other) const noexcept
    {
        return nlri == other.nlri &&
               pathId == other.pathId &&
               &sourceNeighbor == &other.sourceNeighbor;
    }
};

template <typename N>
struct LocalRoute
{
    InboundRoute<N>& in;
};

template <typename N>
struct OutboundRoute : RouteBase
{
    N nlri;

    bool operator==(const OutboundRoute& other) const noexcept
    {
        return nlri == other.nlri &&
               pathId == other.pathId;
    }
};

template <typename N>
using PerPeerInTable = std::unordered_map<N, InboundRoute<N>>;

template <typename N>
using PerPeerOutTable = std::unordered_map<N, OutboundRoute<N>>;

template <typename N>
using AdjRibInTable = std::unordered_map<uint32_t, PerPeerInTable<N>>;

template <typename N>
using AdjRibOutTable = std::unordered_map<uint32_t, PerPeerOutTable<N>>;

template <typename N>
using LocRibTable = std::unordered_map<N, LocalRoute<N>>;
}

#endif // BGP_RIB_TYPES_HPP
