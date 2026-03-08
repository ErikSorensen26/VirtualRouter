// BestPath.cpp

#include "BestPath.h"
#include "bgp/BgpProcess.h"

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

BestPathComparator::BestPathComparator(BgpProcess& p)
    : proc(p) {}

inline bool BestPathComparator::compareMed(const InboundRouteBase& lhs, const InboundRouteBase& rhs) const
{
    if (!proc.getConfigs().get<Config::Bgp::BGP_ALWAYS_COMPARE_MED>().load() && lhs.peerAs != rhs.peerAs)
        return false;
    return medOrDefault(*lhs.getPathAttributes()) < medOrDefault(*rhs.getPathAttributes());
}

bool BestPathComparator::better(const InboundRouteBase& lhs, const IPAddress& lhsNbr, const InboundRouteBase& rhs, const IPAddress& rhsNbr) const
{
    PathAttribute lhsAttr = *lhs.getPathAttributes();
    PathAttribute rhsAttr = *rhs.getPathAttributes();

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

    // 7) eBGP preferred over iBGP
    if (lhs.ebgp != rhs.ebgp)
        return rhs.ebgp;

    // 8) Lowest IGP metric to NEXT_HOP
    if (lhs.igpCost != rhs.igpCost)
        return lhs.igpCost < rhs.igpCost;

    // 9) Oldest route.
    if (lhs.receivedTime != rhs.receivedTime)
        return lhs.receivedTime < rhs.receivedTime;

    // 10) Lowest router-id, then neighbor address.
    uint32_t lhsRid = lhs.sourceNeighbor ? lhs.sourceNeighbor->globalNbr().rid : proc.getRouterId();
    uint32_t rhsRid = rhs.sourceNeighbor ? rhs.sourceNeighbor->globalNbr().rid : proc.getRouterId();
    if (lhsRid != rhsRid)
        return lhsRid < rhsRid;

    // 11) Lowest neighbor address
    return lhsNbr < rhsNbr;
}
}
