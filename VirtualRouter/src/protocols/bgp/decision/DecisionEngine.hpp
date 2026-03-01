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
            if (comparator.better(canidate, canidate.attributes, *best, best->attributes))
                best = &canidates[i];
        }

        return *best;
    }

    template <typename N>
    bool equivalent(const RouteCanidate<N>& lhs, const RouteCanidate<N>& rhs) const
    {
        if (comparator.better(lhs, lhs.attributes, rhs, rhs.attributes))
            return false;
        if (comparator.better(rhs, rhs.attributes, lhs, lhs.attributes))
            return false;
        return true;
    }

private:
    BestPathComparator comparator;
};
}

#endif // BGP_DECISION_ENGINE_H
