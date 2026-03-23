// BestPath.h

#ifndef BGP_BEST_PATH_H
#define BGP_BEST_PATH_H

#include "bgp/rib/RibTypes.hpp"

namespace routing::bgp
{
class BgpProcess;

struct BestPathConfig
{
    bool compareRouterId   = false; // bgp bestpath compare-routerid
    bool medMissingAsWorst = false; // bgp bestpath med missing-as-worst
    bool ignoreIgpMetric   = false; // bgp bestpath igp-metric-ignore
};

class BestPathComparator
{
public:
    explicit BestPathComparator(BgpProcess& p, BestPathConfig cfg = {});

    bool better(const InboundRouteBase& lhsRoute, const types::IPAddress& lhsNbr, const InboundRouteBase& rhsRoute, const types::IPAddress& rhsNbr) const;
private:
    inline bool compareMed(const InboundRouteBase& lhsRoute, const InboundRouteBase& rhsRoute) const;

    BgpProcess& proc;
    BestPathConfig config;
};
} // namespace routing

#endif //BGP_BEST_PATH_H

