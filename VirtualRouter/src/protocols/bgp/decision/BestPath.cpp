// BestPath.cpp

#include "BestPath.h"

namespace BGP
{
uint32_t localPrefOrDefault(const PathAttribute& a)
{
    return a.attrs.localPref.value_or(100);
}

uint32_t medOrDefault(const PathAttribute& a)
{
    return a.attrs.med.value_or(0);
}

uint8_t originRank(const PathAttribute& a)
{
    return static_cast<uint8_t>(a.attrs.origin.value_or(BGP_ORIGIN_INCOMPLETE));
}

BestPathComparator::BestPathComparator(BestPathOptions opts)
    : options(opts) {}

inline bool BestPathComparator::compareMed(const InboundRouteBase& lhs, const InboundRouteBase& rhs) const
{
    if (!options.alwaysCompareMed && lhs.peerAs != rhs.peerAs)
        return false;
    return medOrDefault(*lhs.getPathAttributes()) < medOrDefault(*rhs.getPathAttributes());
}

bool BestPathComparator::better(const InboundRouteBase& lhs, const IPAddress& lhsNbr, const InboundRouteBase& rhs, const IPAddress& rhsNbr) const
{
    PathAttribute lhsAttr = *lhs.getPathAttributes();
    PathAttribute rhsAttr = *rhs.getPathAttributes();

    // 1) Highest local-pref
    if (localPrefOrDefault(lhsAttr) != localPrefOrDefault(rhsAttr))
        return localPrefOrDefault(lhsAttr) > localPrefOrDefault(rhsAttr);

    // 2) Shortest AS_PATH
    if (lhsAttr.attrs.asPathLength() != rhsAttr.attrs.asPathLength())
        return lhsAttr.attrs.asPathLength() < rhsAttr.attrs.asPathLength();

    // 3) Lowest ORIGIN code.
    if (originRank(lhsAttr) != originRank(rhsAttr))
        return originRank(lhsAttr) < originRank(rhsAttr);

    // 4) Lowest MED (same neighboring AS unless always-compare-med).
    if (compareMed(lhs, rhs))
        return true;
    if (compareMed(rhs, lhs))
        return false;

    // 5) eBGP preferred over iBGP
    if (lhs.ebgp != rhs.ebgp)
        return rhs.ebgp;

    // 6) Lowest IGP metric to NEXT_HOP
    if (lhs.igpCost != rhs.igpCost)
        return lhs.igpCost < rhs.igpCost;

    // 7) Oldest route.
    if (lhs.receivedTime != rhs.receivedTime)
        return lhs.receivedTime < rhs.receivedTime;

    // 8) Lowest router-id, then neighbor address.
    if (lhs.neighborRouterId != rhs.neighborRouterId)
        return lhs.neighborRouterId < rhs.neighborRouterId;

    return lhsNbr < rhsNbr;
}
}
