// DecisionEngine.h

#ifndef BGP_DECISION_ENGINE_H
#define BGP_DECISION_ENGINE_H

#include <optional>
#include "BestPath.h"

namespace BGP
{
class DecisionEngine
{
public:
    explicit DecisionEngine(BestPathOptions opts = {})
        : comparator(opts) {}

    template <typename N>
    std::optional<LocalRoute<N>> selectBest(std::vector<InboundRoute<N>*>& canidates) const
    {
        if (canidates.empty())
            return std::nullopt;

        InboundRoute<N>* best = canidates.front();
        for (size_t i = 1; i < canidates.size(); i++)
        {
            InboundRoute<N>* cand = canidates[i];
            const IPAddress& candNbr = cand->sourceNeighbor.globalNbr().neighborAddress;
            const IPAddress& bestNbr = best->sourceNeighbor.globalNbr().neighborAddress;
            if (comparator.better(*cand, candNbr, *best, bestNbr))
                best = canidates[i];
        }

        return LocalRoute<N>{best};
    }

    template <typename N>
    bool equivalent(const InboundRouteBase& lhs, const IPAddress& lhsNbr, const InboundRouteBase& rhs, const IPAddress& rhsNbr)
    {
        if (comparator.better(lhs, lhsNbr, rhs, rhsNbr))
            return false;
        if (comparator.better(rhs, lhsNbr, lhs, rhsNbr))
            return false;
        return true;
    }

private:
    BestPathComparator comparator;
};
}

#endif // BGP_DECISION_ENGINE_H
