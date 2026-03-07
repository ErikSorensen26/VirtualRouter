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
    std::optional<RouteCanidate<N>> selectBest(const std::vector<RouteCanidate<N>>& canidates) const
    {
        if (canidates.empty())
            return std::nullopt;

        const RouteCanidate<N>* best = &canidates.front();
        for (size_t i = 1; i < canidates.size(); i++)
        {
            auto& canidate = canidates[i];
            PathAttribute cpa{canidate.attrs, canidate.path};
            PathAttribute bpa{best->attrs, best->path};
            if (comparator.better(canidate, cpa, *best, bpa))
                best = &canidates[i];
        }

        return *best;
    }

    template <typename N>
    bool equivalent(const RouteCanidate<N>& lhs, const RouteCanidate<N>& rhs) const
    {
        PathAttribute lpa{lhs.attrs, lhs.path};
        PathAttribute rpa{rhs.attrs, rhs.path};
        if (comparator.better(lhs, lpa, rhs, rpa))
            return false;
        if (comparator.better(rhs, rpa, lhs, lpa))
            return false;
        return true;
    }

private:
    BestPathComparator comparator;
};
}

#endif // BGP_DECISION_ENGINE_H
