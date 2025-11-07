// RouteAggregator.h

#ifndef EIGRP_ROUTE_AGGREGATOR_H
#define EIGRP_ROUTE_AGGREGATOR_H

#include <TopologyTable.h>

struct IPAddress;

namespace Eigrp
{
struct SummaryRoute
{
    RouteInfo summaryRoute;
    bool isAuto = false;
    bool supressed = false;
    std::set<IPPrefix> summarizedRoutes;
};

class RouteAggregator
{
public:
    RouteAggregator(EigrpInterface& iface);
    ~RouteAggregator();

    void clearAutoSummaries();
    void updateSummaryRoute(SummaryRoute& r, ReceivedRoute& route);
    bool calculateSummary(SummaryRoute& r);
    void updateAllSummaryRoutes();
    void installSummary(const IPPrefix& prefix, bool isAuto = false);
    void withdrawSummary(const IPPrefix& prefix);

    SummaryRoute* isSummarized(const IPPrefix& prefix);

private:
    EigrpInterface& iface;
    std::mutex mtx;
    std::map<IPPrefix, SummaryRoute> summaryRoutes;
};
}

#endif // EIGRP_ROUTE_AGGREGATOR_H
