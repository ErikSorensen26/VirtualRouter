// RouteAggregator.h

#ifndef EIGRP_ROUTE_AGGREGATOR_H
#define EIGRP_ROUTE_AGGREGATOR_H

#include <map>
#include <set>
#include <limits>
#include <IPAddress.h>

struct IPAddress;

namespace EIGRP
{
class EigrpInterface;

struct TopologyEntry;
struct RouteInfo;

struct SummaryRoute
{
    TopologyEntry* summaryEntry = nullptr;
    RouteInfo* summaryRoute = nullptr;
    bool isAuto = false;
    std::set<IPPrefix> summarizedRoutes = {};
    uint64_t bestFD = std::numeric_limits<uint64_t>::max();
};

class RouteAggregator
{
public:
    RouteAggregator(EigrpInterface& iface);
    ~RouteAggregator();

    void clearAutoSummaries();
    void updateSummaryRoute(SummaryRoute& r);
    void updateSummaryRoutes(std::vector<SummaryRoute*>& r);
    std::pair<bool, bool> calculateSummary(SummaryRoute& r);
    void updateAllSummaryRoutes(bool isAuto = false);
    void installSummary(const IPPrefix& prefix, bool isAuto = false);
    void installSummaries(const std::set<IPPrefix>& prefixes, bool isAuto = false);
    void withdrawSummary(const IPPrefix& prefix);

    SummaryRoute* isSummarized(const IPPrefix& prefix);

private:
    EigrpInterface& iface;
    std::map<IPPrefix, SummaryRoute> summaryRoutes;
};
}

#endif // EIGRP_ROUTE_AGGREGATOR_H
