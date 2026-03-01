// BestPath.cpp

#include "BestPath.h"

namespace BGP
{
uint32_t localPrefOrDefault(const PathAttributeBase& a)
{
    return a.localPref.value_or(100);
}

uint32_t medOrDefault(const PathAttributeBase& a)
{
    return a.med.value_or(0);
}

uint8_t originRank(const PathAttributeBase& a)
{
    return static_cast<uint8_t>(a.origin.value_or(BGP_ORIGIN_INCOMPLETE));
}

BestPathComparator::BestPathComparator(BestPathOptions opts)
    : options(opts) {}

inline bool BestPathComparator::compareMed(const RouteCanidateBase& lhsRoute, const PathAttributeBase& lhsAttr, const RouteCanidateBase& rhsRoute, const PathAttributeBase& rhsAttr) const
{
    if (!options.alwaysCompareMed && lhsRoute.peerAs != rhsRoute.peerAs)
        return false;
    return medOrDefault(lhsAttr) < medOrDefault(rhsAttr);
}

bool BestPathComparator::better(const RouteCanidateBase& lhsRoute, const PathAttributeBase& lhsAttr, const RouteCanidateBase& rhsRoute, const PathAttributeBase& rhsAttr) const
{
    // 1) Highest local-pref
    if (localPrefOrDefault(lhsAttr) != localPrefOrDefault(rhsAttr))
        return localPrefOrDefault(lhsAttr) > localPrefOrDefault(rhsAttr);

    // 2) Shortest AS_PATH
    if (lhsAttr.asPathLength() != rhsAttr.asPathLength())
        return lhsAttr.asPathLength() > rhsAttr.asPathLength();

    // 3) Lowest ORIGIN code.
    if (originRank(lhsAttr) != originRank(rhsAttr))
        return originRank(lhsAttr) < originRank(rhsAttr);

    // 4) Lowest MED (same neighboring AS unless always-compare-med).
    if (compareMed(lhsRoute, lhsAttr, rhsRoute, rhsAttr))
        return true;
    if (compareMed(rhsRoute, rhsAttr, lhsRoute, rhsAttr))
        return false;

    // 5) eBGP preferred over iBGP
    if (lhsRoute.ebgp != rhsRoute.ebgp)
        return lhsRoute.ebgp;

    // 6) Lowest IGP metric to NEXT_HOP
    if (lhsRoute.igpCost != rhsRoute.igpCost)
        return lhsRoute.igpCost < rhsRoute.igpCost;

    // 7) Oldest route.
    if (lhsRoute.receivedTime != rhsRoute.receivedTime)
        return lhsRoute.receivedTime < rhsRoute.receivedTime;

    // 8) Lowest router-id, then neighbor address.
    if (lhsRoute.neighborRouterId != rhsRoute.neighborRouterId)
        return lhsRoute.neighborRouterId < rhsRoute.neighborRouterId;

    return lhsRoute.neighborAddress < rhsRoute.neighborAddress;
}
}
