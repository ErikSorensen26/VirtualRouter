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
template <typename N>
struct BuildUpdate
{
    struct Announcement
    {
        PathAttribute attrs;
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

struct RouteCanidateBase
{
    RouteCanidateBase() = default;

    explicit RouteCanidateBase(AttributeManager& mgr)
        : attrMgr(&mgr)
    {}

    RouteCanidateBase(AttributeManager& mgr, const Path& p, const Attributes& a)
        : attrs(a),
          path(p),
          attrMgr(&mgr)
    {
        acquirePathRef();
    }

    RouteCanidateBase(const RouteCanidateBase& other)
        : attrs(other.attrs),
          path(other.path),
          pathId(other.pathId),
          hasPathRef(other.hasPathRef),
          peerAs(other.peerAs),
          neighborRouterId(other.neighborRouterId),
          neighborAddress(other.neighborAddress),
          ebgp(other.ebgp),
          igpCost(other.igpCost),
          receivedTime(other.receivedTime),
          attrMgr(other.attrMgr)
    {
        retainPathRef();
    }

    RouteCanidateBase(RouteCanidateBase&& other) noexcept
        : attrs(std::move(other.attrs)),
          path(std::move(other.path)),
          pathId(other.pathId),
          hasPathRef(other.hasPathRef),
          peerAs(other.peerAs),
          neighborRouterId(other.neighborRouterId),
          neighborAddress(std::move(other.neighborAddress)),
          ebgp(other.ebgp),
          igpCost(other.igpCost),
          receivedTime(other.receivedTime),
          attrMgr(other.attrMgr)
    {
        other.pathId = 0;
        other.hasPathRef = false;
        other.attrMgr = nullptr;
    }

    RouteCanidateBase& operator=(const RouteCanidateBase& other)
    {
        if (this == &other)
            return *this;

        releasePathRef();

        attrs = other.attrs;
        path = other.path;
        pathId = other.pathId;
        hasPathRef = other.hasPathRef;
        peerAs = other.peerAs;
        neighborRouterId = other.neighborRouterId;
        neighborAddress = other.neighborAddress;
        ebgp = other.ebgp;
        igpCost = other.igpCost;
        receivedTime = other.receivedTime;
        attrMgr = other.attrMgr;

        retainPathRef();
        return *this;
    }

    RouteCanidateBase& operator=(RouteCanidateBase&& other) noexcept
    {
        if (this == &other)
            return *this;

        releasePathRef();

        attrs = std::move(other.attrs);
        path = std::move(other.path);
        pathId = other.pathId;
        hasPathRef = other.hasPathRef;
        peerAs = other.peerAs;
        neighborRouterId = other.neighborRouterId;
        neighborAddress = std::move(other.neighborAddress);
        ebgp = other.ebgp;
        igpCost = other.igpCost;
        receivedTime = other.receivedTime;
        attrMgr = other.attrMgr;

        other.pathId = 0;
        other.hasPathRef = false;
        other.attrMgr = nullptr;

        return *this;
    }

    ~RouteCanidateBase()
    {
        releasePathRef();
    }

    void setPathAttributes(const PathAttribute& pa)
    {
        releasePathRef();
        attrs = pa.attrs;
        path = pa.path;
        acquirePathRef();
    }

    bool hasInternedPath() const noexcept
    {
        return hasPathRef;
    }

    Attributes attrs{};
    Path path{};
    uint32_t pathId = 0;
    bool hasPathRef = false;
    uint32_t peerAs = 0;
    uint32_t neighborRouterId = 0;
    IPAddress neighborAddress;
    bool ebgp = true;

    uint64_t igpCost = std::numeric_limits<uint64_t>::max();
    std::chrono::steady_clock::time_point receivedTime = std::chrono::steady_clock::now();

private:
    void acquirePathRef()
    {
        if (!attrMgr)
            return;

        pathId = attrMgr->acquire(attrs, path);
        hasPathRef = true;
    }

    void retainPathRef()
    {
        if (!attrMgr || !hasPathRef)
            return;

        if (!attrMgr->retain(pathId))
            acquirePathRef();
    }

    void releasePathRef()
    {
        if (attrMgr && hasPathRef)
            attrMgr->release(pathId);

        pathId = 0;
        hasPathRef = false;
    }

    AttributeManager* attrMgr = nullptr;
};

template <typename N>
struct RouteCanidate : RouteCanidateBase
{
    N nlri;

    RouteCanidate() = default;
    explicit RouteCanidate(AttributeManager& mgr)
        : RouteCanidateBase(mgr)
    {}

    RouteCanidate(AttributeManager& mgr, const N& n, const PathAttribute& pa)
        : RouteCanidateBase(mgr, pa.path, pa.attrs),
          nlri(n)
    {}

    bool operator==(const RouteCanidate<N>& other) const noexcept
    {
        return nlri == other.nlri &&
               pathId == other.pathId &&
               peerAs == other.peerAs &&
               neighborRouterId == other.neighborRouterId &&
               neighborAddress == other.neighborAddress;
    }
};

template <typename N>
using PerPeerAdjTable = std::unordered_map<N, RouteCanidate<N>>;

template <typename N>
using AdjRibInTable = std::unordered_map<uint32_t, PerPeerAdjTable<N>>;

template <typename N>
using AdjRibOutTable = std::unordered_map<uint32_t, PerPeerAdjTable<N>>;

template <typename N>
using LocRibTable = std::unordered_map<N, RouteCanidate<N>>;
}

#endif // BGP_RIB_TYPES_HPP
