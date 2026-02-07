// SpfVertex.hpp

#ifndef SPF_VERTEX_HPP
#define SPF_VERTEX_HPP

#include <cstdint>
#include <limits>
#include <functional>
#include <LsaKey.hpp>
#include <LSDB.hpp>

#define OSPF_DETERMINISTIC_PARENT_ORDER true
#define OSPF_STRICT_MISSING_NETWORK_LSA false

namespace OSPF
{

enum class VertexType : uint8_t
{
    ROUTER = 1,
    NETWORK = 2
};

struct Vertex
{
    VertexType type{};
    uint64_t id; // Router-ID

    friend bool operator==(const Vertex& a, const Vertex& b)
    {
        return a.type == b.type && a.id == b.id;
    }

};

struct VertexHash
{
    size_t operator()(const OSPF::Vertex& v) const noexcept
    {
        return (static_cast<size_t>(v.type) << 1) ^ (static_cast<size_t>(v.id) * 0x9e3779b97f4a7c15ull);
    }
};

struct EdgeKey
{
    Vertex from{};
    Vertex to{};
    uint32_t lastHopIfid{0};

    friend bool operator==(const EdgeKey& a, const EdgeKey& b) noexcept
    {
        return a.from == b.from && a.to == b.to && a.lastHopIfid == b.lastHopIfid;
    }
};

struct EdgeKeyHash
{
    size_t operator()(const EdgeKey& k) const noexcept
    {
        size_t h = 0;
        h ^= VertexHash{}(k.from) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= VertexHash{}(k.to)   + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(k.lastHopIfid) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    }
};

struct EdgeVal
{
    uint32_t cost{0};
};

struct BacklinkKey
{
    VertexType t1;
    uint64_t id1;
    VertexType t2;
    uint64_t id2;

    BacklinkKey(VertexType aType, uint64_t aId, VertexType bType, uint64_t bId)
    {
        if (aType < bType || (aType == bType && aId <= bId))
        {
            t1 = aType; id1 = aId;
            t2 = bType; id2 = bId;
        }
        else
        {
            t1 = bType; id1 = bId;
            t2 = aType; id2 = aId;
        }
    }

    bool operator==(const BacklinkKey& o) const noexcept
    {
        return t1 == o.t1 && id1 == o.id1 && t2 == o.t2 && id2 == o.id2;
    }
};

struct BacklinkKeyHash
{
    size_t operator()(const BacklinkKey& k) const noexcept
    {
        size_t h = 0;
        h ^= std::hash<uint64_t>{}((uint64_t(k.t1) << 56) | k.id1);
        h ^= std::hash<uint64_t>{}((uint64_t(k.t2) << 56) | k.id2);
        return h;
    }
};

static inline uint64_t packNetwork(uint32_t advRouter, uint32_t lsId)
{
    return (static_cast<uint64_t>(advRouter) << 32) | static_cast<uint64_t>(lsId);
}

static inline uint32_t networkAdvRouter(uint64_t packed) { return static_cast<uint32_t>(packed >> 32); }
static inline uint32_t networkLsId(uint64_t packed) { return static_cast<uint32_t>(packed & 0xFFFFFFFFu); }
static inline LsaKey networkLsaKey(uint64_t packed, uint16_t type) { return LsaKey{type, networkLsId(packed), networkAdvRouter(packed)};}

static inline bool vertexLess(const Vertex& a, const Vertex& b)
{
    if (a.type != b.type) return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
    return a.id < b.id;
}

struct SpfDelta
{
    struct Change
    {
        enum class Kind : uint8_t
        {
            ADD,
            REMOVE,
            COST_DECREASE,
            COST_INCREASE
        };

        Kind kind{};
        EdgeKey key{};
        uint32_t oldCost{0};
        uint32_t newCost{0};
    };

    bool hasAnyChange{false};
    bool hasAnyIncreaseOrRemove{false};
    std::vector<Change> changes;
};

struct ParentRef
{
    Vertex parent{};
    uint32_t firstHopIfid{0};
    uint32_t lastHopIfid{0};
    uint32_t edgeCost{0};
};

struct SpfEdge
{
    Vertex to{};
    uint32_t cost{0};
    uint32_t ifid{0};
};

struct SptNode
{
    uint64_t dist = std::numeric_limits<uint64_t>::max();
    bool confirmed = false;
    std::vector<ParentRef> parents;
};

struct SpfResult
{
    Vertex root;
    std::unordered_map<Vertex, SptNode, VertexHash> nodes;
    std::vector<Vertex> confirmedOrder;
};

template <typename PQ>
struct RelaxInfo
{
    RelaxInfo(SpfResult& r, PQ& q)
        : out(r), pq(q) {}
    
    bool repairMode{false};

    SpfResult& out;
    PQ& pq;
};
}

#endif // SPF_VERTEX_HPP
