// BestPath.cpp

#include <limits>

#include "BestPath.h"
#include "bgp/BgpProcess.h"

namespace routing::bgp
{
uint32_t localPrefOrDefault(const PathAttribute& a)
{
    return a.attrs.localPref.value_or(100);
}

uint32_t medOrDefault(const PathAttribute& a, bool missingAsWorst)
{
    if (!a.attrs.med.has_value())
        return missingAsWorst ? std::numeric_limits<uint32_t>::max() : 0;
    return *a.attrs.med;
}

uint8_t originRank(const PathAttribute& a)
{
    return static_cast<uint8_t>(a.attrs.origin.value_or(BGP_ORIGIN_INCOMPLETE));
}

BestPathComparator::BestPathComparator(BgpProcess& p, BestPathConfig cfg)
    : proc(p), config(cfg) {}

inline bool BestPathComparator::compareMed(const InboundRouteBase& lhs, const InboundRouteBase& rhs) const
{
    if (!config.compareMed)
        return false;
    return medOrDefault(lhs.getPathAttributes(), config.medMissingAsWorst)
         < medOrDefault(rhs.getPathAttributes(), config.medMissingAsWorst);
}

bool BestPathComparator::better(const InboundRouteBase& lhs, const types::IPAddress& lhsNbr, const InboundRouteBase& rhs, const types::IPAddress& rhsNbr) const
{
    PathAttribute lhsAttr = lhs.getPathAttributes();
    PathAttribute rhsAttr = rhs.getPathAttributes();

    // 1) Highest weight
    if (lhs.weigth != rhs.weigth)
        return lhs.weigth > rhs.weigth;

    // 2) Highest local-pref
    if (localPrefOrDefault(lhsAttr) != localPrefOrDefault(rhsAttr))
        return localPrefOrDefault(lhsAttr) > localPrefOrDefault(rhsAttr);

    // 3) Locally originated
    if (!lhs.sourceNeighbor && rhs.sourceNeighbor)
        return true;

    // 4) Shortest AS_PATH
    if (lhsAttr.attrs.asPathLength() != rhsAttr.attrs.asPathLength())
        return lhsAttr.attrs.asPathLength() < rhsAttr.attrs.asPathLength();

    // 5) Lowest ORIGIN code.
    if (originRank(lhsAttr) != originRank(rhsAttr))
        return originRank(lhsAttr) < originRank(rhsAttr);

    // 6) Lowest MED (same neighboring AS unless always-compare-med).
    if (compareMed(lhs, rhs))
        return true;
    if (compareMed(rhs, lhs))
        return false;

    // 7) eBGP / confederation-eBGP preferred over iBGP (RFC 3065 §5).
    const bool lhsExternal = lhs.ebgp || lhs.confedEbgp;
    const bool rhsExternal = rhs.ebgp || rhs.confedEbgp;
    if (lhsExternal != rhsExternal)
        return lhsExternal;

    // 8) Lowest IGP metric to NEXT_HOP
    if (!config.ignoreIgpMetric && lhs.igpCost != rhs.igpCost)
        return lhs.igpCost < rhs.igpCost;

    // 9/10) Oldest route (stability) vs lowest router-ID (deterministic), depending on config.
    if (!config.compareRouterId)
    {
        // Without compare-routerid: prefer the older (stable) route; no router-ID step.
        if (lhs.receivedTime != rhs.receivedTime)
            return lhs.receivedTime < rhs.receivedTime;
    }
    else
    {
        // With compare-routerid: prefer lowest router-ID (deterministic, skip oldest-route step).
        uint32_t lhsRid = lhs.sourceNeighbor ? lhs.sourceNeighbor->getParent().getRouterId() : proc.getRouterId();
        uint32_t rhsRid = rhs.sourceNeighbor ? rhs.sourceNeighbor->getParent().getRouterId() : proc.getRouterId();
        if (lhsRid != rhsRid)
            return lhsRid < rhsRid;
    }

    // Final) Lowest neighbor address
    return lhsNbr < rhsNbr;
}
} // namespace routing
