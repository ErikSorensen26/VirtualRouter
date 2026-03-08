// BestPath.h

#ifndef BGP_BEST_PATH_H
#define BGP_BEST_PATH_H

#include "bgp/rib/RibTypes.hpp"

namespace BGP
{
struct BestPathOptions
{
    bool alwaysCompareMed = false;
};

class BestPathComparator
{
public:
    explicit BestPathComparator(BestPathOptions opts = {});

    bool better(const InboundRouteBase& lhsRoute, const IPAddress& lhsNbr, const InboundRouteBase& rhsRoute, const IPAddress& rhsNbr) const;
private:
    inline bool compareMed(const InboundRouteBase& lhsRoute, const InboundRouteBase& rhsRoute) const;

    BestPathOptions options;
};
}

#endif //BGP_BEST_PATH_H
