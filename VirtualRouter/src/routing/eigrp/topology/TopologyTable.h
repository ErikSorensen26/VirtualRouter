// TopologyTable

#ifndef EIGRP_TOPOLOGY_TABLE_H
#define EIGRP_TOPOLOGY_TABLE_H

#include <cstdint>
#include <IPAddress.h>
#include <chrono>
#include <map>
#include <unordered_set>

namespace routing::eigrp
{
class EigrpInterface;
class Eigrp;
class Neighbor;
struct ReceivedRoute;
struct SummaryRoute;

enum class RouteType { INTERNAL, EXTERNAL, SUMMARY, CONNECTED, STATIC, WITHDRAW };

struct ReceivedRoute
{
    types::IPPrefix prefix;
    types::IPAddress nextHop;
    uint32_t originInterface;
    uint64_t reportedDistance;
    uint64_t feasibleDistance;
    uint64_t delay;
    uint64_t bandwidth;
    uint16_t mtu;
    uint8_t reliability;
    uint8_t load;
    uint8_t hopCount;
    uint32_t tag;
    uint8_t adminDistance;

    struct External
    {
        uint32_t originRouter;
        uint32_t originAS;
        uint32_t externalMetric;
        uint8_t type;
        uint8_t flags;

        enum class ExternalFlags : uint8_t
        {
            EXTERNAL      = 0x01,
            DEFAULT = 0x02,
        };

        inline void setFlag(ExternalFlags bit) { flags |= static_cast<uint8_t>(bit); }
        inline void clearFlag(ExternalFlags bit) { flags &= ~static_cast<uint8_t>(bit); }
        inline void toggleFlag(ExternalFlags bit) { flags ^= static_cast<uint8_t>(bit); }
        inline bool hasFlag(ExternalFlags bit) { return flags & static_cast<uint8_t>(bit); }
    } external;

    struct Wide
    {
        bool isWide = false;
        uint16_t topology = 0;
        uint16_t afi = 0;
        uint32_t rid = 0;
        uint8_t priority = 0;

        std::vector<uint8_t> data;
        void allocate(const uint8_t* src, size_t n)
        {
            data.assign(src, src + n);
        }

        uint8_t* bytes() { return data.data(); }
        size_t size() const { return data.size(); }

        Wide() = default;
        ~Wide() = default;
        Wide(const Wide&) = default;
        Wide& operator=(const Wide&) = default;
        Wide(Wide&&) noexcept = default;
        Wide& operator=(Wide&&) noexcept = default;
    } wide;
    
    uint8_t flags = 0;

    enum class RouteFlags : uint8_t
    {
        WITHDRAWL = 0x01,
        DEFAULT = 0x02,
        ACTIVE = 0x04,
        REPLICATED = 0x08
    };

    inline void setFlag(RouteFlags bit) { flags |= static_cast<uint8_t>(bit); }
    inline void clearFlag(RouteFlags bit) { flags &= ~static_cast<uint8_t>(bit); }
    inline void toggleFlag(RouteFlags bit) { flags ^= static_cast<uint8_t>(bit); }
    inline bool hasFlag(RouteFlags bit) { return flags & static_cast<uint8_t>(bit); }

    RouteType routeType{};

    ReceivedRoute() = default;
    ~ReceivedRoute() = default;

    ReceivedRoute(const ReceivedRoute&) = default;
    ReceivedRoute& operator=(const ReceivedRoute&) = default;
    ReceivedRoute(ReceivedRoute&&) noexcept = default;
    ReceivedRoute& operator=(ReceivedRoute&&) noexcept = default;
};

struct TopologyEntry;
struct RouteInfo
{
    RouteInfo(ReceivedRoute& rt) : routeInfo(std::move(rt)) {}
    ReceivedRoute routeInfo;
    bool isSuccessor = false;
    bool isFeasibleSuccessor = false;
    bool notFeasible = false;
    const TopologyEntry* topology = nullptr;
    std::chrono::steady_clock::time_point lastUpdate;
};

struct SuppressionInfo
{
    std::unordered_set<SummaryRoute*> summaries;
    bool isSuppressed()
    {
        return !summaries.empty();
    }
};

struct TopologyEntry
{
    enum class State { ACTIVE, PASSIVE, POISENED };
    types::IPPrefix prefix;
    std::map<types::IPAddress, RouteInfo> routesBySource; ///< Routes learned from each neighbor.

    std::vector<types::IPAddress> feasibleSuccessors; ///< List of feasible successor neighbors.
    std::vector<types::IPAddress> successors; ///< List of successor neighbors.

    uint64_t bestFD = std::numeric_limits<uint64_t>::max();
    uint8_t bestAD = std::numeric_limits<uint8_t>::max();
    types::IPAddress bestNeighbor = {};

    std::map<uint32_t, SuppressionInfo> suppression;
    bool isSuppressed(uint32_t key) const
        { return suppression.count(key) > 0; }

    State state = State::PASSIVE;
    std::optional<std::chrono::steady_clock::time_point> valid = std::nullopt;
};

class TopologyTable
{
public:

    TopologyTable(Eigrp& process);
    ~TopologyTable();
    RouteInfo& addRouteUpdate(const ReceivedRoute& route, const Neighbor* neighborIp, TopologyEntry& entry);
    void markRouteUnreachable(RouteInfo& route, const types::IPAddress& neighborIp, TopologyEntry& entry);
    void pruneExpired();
    void pruneNeighbor(const types::IPAddress& neighborIp);
    std::pair<TopologyEntry*, RouteInfo*> findPair(const types::IPPrefix& prefix, const types::IPAddress& neighbor);
    TopologyEntry& ensure(const types::IPPrefix& prefix);
    TopologyEntry* find(const types::IPPrefix& prefix);
    std::unordered_map<types::IPPrefix, TopologyEntry>& entries() { return topologyEntries; }

private:
    std::unordered_map<types::IPPrefix, TopologyEntry> topologyEntries;

    Eigrp& eigrpProcess;
};
} // namespace routing

#endif // EIGRP_TOPOLOGY_TABLE_H

