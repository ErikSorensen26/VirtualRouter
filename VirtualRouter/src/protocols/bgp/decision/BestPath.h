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

    bool better(const RouteCanidateBase& lhsRoute, const PathAttribute& lhsAttr, const RouteCanidateBase& rhsRoute, const PathAttribute& rhsAttr) const;
private:
    inline bool compareMed(const RouteCanidateBase& lhsRoute, const PathAttribute& lhsAttr, const RouteCanidateBase& rhsRoute, const PathAttribute& rhsAttr) const;

    BestPathOptions options;
};
}

#endif //BGP_BEST_PATH_H
