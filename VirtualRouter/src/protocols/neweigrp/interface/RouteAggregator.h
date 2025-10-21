// RouteAggregator.h

#ifndef ROUTE_AGGREGATOR_H
#define ROUTE_AGGREGATOR_H

#include <cstdint>

struct IPAddress;
class RoutingTable { public: struct Eigrp; };
namespace EigrpConfigs
{
struct SummaryRoute;
}

namespace Protocol
{
class RouteAggregator
{
public:

    /**
     * @brief Advertises a summary route to a neighbor.
     *
     * Constructs and sends a summary route advertisement to the specified neighbor,
     * consolidating multiple routes into a single summary route as per EIGRP's
     * summarization policies.
     *
     * @param summaryRoute Summary route information.
     */
    void announceSummary(RoutingTable::Eigrp* summaryRoute);

    /**
     * @brief Withdraws a summary route from a neighbor.
     *
     * Sends a Withdraw (WITHDRAW) packet to the neighbor to remove the previously advertised
     * summary route, ensuring that outdated or no longer valid summary routes are cleaned up.
     *
     * @param route Summary route information.
     */
    void withdrawSummary(RoutingTable::Eigrp* route);

    /**
     * @brief Removes all auto summarized route on an interface
     *
     * Finds all summarized routes and removes all automatically assigned summarizations
     * for the specific interface.
     */
    void clearAutoSummaries();

    /**
     * @brief Adds a summary route to the EIGRP configuration.
     *
     * Configures a summary route, aggregating multiple routes into a single summary
     * entry to reduce routing table size and improve network efficiency.
     *
     * @param network Network address of the summary route.
     * @param mask Subnet mask of the summary route.
     * @param isAuto Indicates if the summary route is auto-generated.
     */
    void addManualSummary(const uint8_t* network, uint8_t mask, bool isAuto = false);

    /**
     * @brief Removes a summary route from the EIGRP configuration.
     *
     * Deletes a previously configured summary route, ensuring that outdated or
     * unnecessary summary routes are no longer advertised or maintained.
     *
     * @param network Network address of the summary route.
     * @param mask Subnet mask of the summary route.
     */
    void removeManuelSummary(const IPAddress& network, uint8_t mask);

    /**
     * @brief 
     */
    void restoreSummaryRoutes(const IPAddress& summaryNetwork, uint8_t summaryMask);

    /**
     * @brief Checks if a route matches any configured summary route.
     *
     * Verifies whether the specified route falls under any of the configured summary
     * routes, aiding in route aggregation and advertisement decisions.
     *
     * @param network Network address.
     * @param mask Subnet mask.
     * @return True if the route is summarized, false otherwise.
     */
    EigrpConfigs::SummaryRoute* isSummarized(const uint8_t* network, uint8_t mask);
};
}

#endif // RouteAggregator
