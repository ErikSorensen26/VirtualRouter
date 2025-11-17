// TopologyTable

#ifndef EIGRP_TOPOLOGY_TABLE_H
#define EIGRP_TOPOLOGY_TABLE_H

#include <cstdint>
#include <IPAddress.hpp>
#include <chrono>
#include <mutex>
#include <map>

namespace Eigrp
{
class EigrpInterface;
class Eigrp;
class Neighbor;
struct ReceivedRoute;
struct SummaryRoute;

enum class RouteType { INTERNAL, EXTERNAL, SUMMARY, CONNECTED, STATIC, WITHDRAW };

struct ReceivedRoute
{
    IPPrefix prefix;
    IPAddress nextHop;
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
        uint16_t topology;
        uint16_t afi;
        uint32_t rid;
        uint8_t priority;
        uint16_t wideFlags;

        enum class WideFlags : uint16_t
        {
            WITHDRAWL = 0x0001,
            DEFAULT = 0x0002,
            ACTIVE = 0x0004,
            REPLICATED = 0x0008
        };

        inline void setFlag(WideFlags bit) { wideFlags |= static_cast<uint16_t>(bit); }
        inline void clearFlag(WideFlags bit) { wideFlags &= ~static_cast<uint16_t>(bit); }
        inline void toggleFlag(WideFlags bit) { wideFlags ^= static_cast<uint16_t>(bit); }
        inline bool hasFlag(WideFlags bit) { return wideFlags & static_cast<uint16_t>(bit); }

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

    RouteType routeType{};

    ReceivedRoute() = default;
    ~ReceivedRoute() = default;

    ReceivedRoute(const ReceivedRoute&) = default;
    ReceivedRoute& operator=(const ReceivedRoute&) = default;
    ReceivedRoute(ReceivedRoute&&) noexcept = default;
    ReceivedRoute& operator=(ReceivedRoute&&) noexcept = default;
};

struct RouteInfo
{
    RouteInfo(ReceivedRoute& rt) : routeInfo(std::move(rt)) {}
    ReceivedRoute routeInfo;
    bool isSuccessor = false;
    bool isFeasibleSuccessor = false;
    bool notFeasible = false;
    std::chrono::steady_clock::time_point lastUpdate;
    std::optional<std::chrono::steady_clock::time_point> valid = std::nullopt;
};

struct TopologyEntry
{
    std::mutex entryMutex;
    enum class State { ACTIVE, PASSIVE, POISENED };
    IPPrefix prefix;
    std::map<IPAddress, RouteInfo> routesByNeighbor; ///< Routes learned from each neighbor.

    std::vector<IPAddress> feasibleSuccessors; ///< List of feasible successor neighbors.
    std::vector<IPAddress> successors; ///< List of successor neighbors.

    uint64_t bestFD = std::numeric_limits<uint64_t>::max();
    uint8_t bestAD = std::numeric_limits<uint8_t>::max();
    IPAddress bestNeighbor;

    std::unordered_map<uint32_t, SummaryRoute*> summaries;
    std::vector<IPAddress> pendingWithdraws;

    State state = State::PASSIVE;
};

class TopologyTable
{
public:

    TopologyTable(Eigrp& process);
    ~TopologyTable();
    void addRouteUpdate(const ReceivedRoute& route, const Neighbor* neighborIp, TopologyEntry& entry);
    void markRouteUnreachable(RouteInfo& route, const IPAddress& neighborIp, TopologyEntry& entry);
    void pruneExpired();
    void pruneNeighbor(const IPAddress& neighborIp);
    std::pair<TopologyEntry*, RouteInfo*> findPair(const IPPrefix& prefix, const IPAddress& neighbor);
    TopologyEntry& ensure(const IPPrefix& prefix);
    TopologyEntry* find(const IPPrefix& prefix);
    std::vector<const RouteInfo*> getSuccessors(const IPPrefix& prefix);
    std::vector<const RouteInfo*> getAllRoutes();

    std::unordered_map<IPPrefix, TopologyEntry*>& entries() { std::lock_guard<std::mutex> lock(tableMutex); return topologyEntries; }

    std::mutex tableMutex;

private:
    std::unordered_map<IPPrefix, TopologyEntry*> topologyEntries;

    Eigrp& eigrpProcess;
};
}

#endif // EIGRP_TOPOLOGY_TABLE_H
