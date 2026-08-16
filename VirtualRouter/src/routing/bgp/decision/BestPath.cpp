// BestPath.cpp

#include <limits>

#include "BestPath.h"
#include "bgp/BgpScope.h"
#include "bgp/af/ScopeAccessor.h"

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

BestPathComparator::BestPathComparator(BgpScope& s, BestPathConfig cfg)
    : scope(s), config(cfg) {}

inline bool BestPathComparator::compareMed(const InboundRouteBase& lhs, const InboundRouteBase& rhs) const
{
    // RFC 4271 9.1.2.2 (c): MED is only comparable between routes learned from the
    // same neighbouring AS. `always-compare-med` lifts that restriction.
    if (!config.compareMed && !sameNeighborAs(lhs, rhs, config.medConfed))
        return false;

    return medOrDefault(lhs.getPathAttributes(), config.medMissingAsWorst)
         < medOrDefault(rhs.getPathAttributes(), config.medMissingAsWorst);
}

bool BestPathComparator::sameNeighborAs(const InboundRouteBase& lhs, const InboundRouteBase& rhs,
                                        bool medConfed)
{
    auto neighborAs = [medConfed](const InboundRouteBase& r) -> uint32_t {
        const auto& attrs = r.getPathAttributes().attrs;
        if (medConfed)
        {
            for (const auto& seg : attrs.asPath)
            {
                if (seg.asns.empty())
                    continue;
                if (seg.segmentType == BGP_AS_CONFED_SEQUENCE ||
                    seg.segmentType == BGP_AS_CONFED_SET)
                    return seg.asns.front();
                break; // a non-confed segment leads: fall through to the normal rule
            }
        }
        uint32_t fa = attrs.firstAs();
        return fa != 0 ? fa : r.peerAs;
    };
    return neighborAs(lhs) == neighborAs(rhs);
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
    if (!lhs.sourceNeighbor != !rhs.sourceNeighbor)
        return !lhs.sourceNeighbor;

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
        uint32_t lhsRid = lhs.sourceNeighbor ? lhs.sourceNeighbor->getParent().getRouterId() : ScopeAccessor::getRid(scope);
        uint32_t rhsRid = rhs.sourceNeighbor ? rhs.sourceNeighbor->getParent().getRouterId() : ScopeAccessor::getRid(scope);
        if (lhsRid != rhsRid)
            return lhsRid < rhsRid;
    }

    // Final) Lowest neighbor address
    return lhsNbr < rhsNbr;
}
} // namespace routing
