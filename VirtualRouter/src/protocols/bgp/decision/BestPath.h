// BestPath.h

#ifndef BGP_BEST_PATH_H
#define BGP_BEST_PATH_H

#include "bgp/rib/RibTypes.hpp"

namespace BGP
{
class BgpProcess;

class BestPathComparator
{
public:
    explicit BestPathComparator(BgpProcess& p);

    bool better(const InboundRouteBase& lhsRoute, const IPAddress& lhsNbr, const InboundRouteBase& rhsRoute, const IPAddress& rhsNbr) const;
private:
    inline bool compareMed(const InboundRouteBase& lhsRoute, const InboundRouteBase& rhsRoute) const;

    BgpProcess& proc;
};
}

#endif //BGP_BEST_PATH_H
