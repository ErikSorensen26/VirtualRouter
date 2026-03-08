// DecisionEngine.h

#ifndef BGP_DECISION_ENGINE_H
#define BGP_DECISION_ENGINE_H

#include <optional>
#include "BestPath.h"

namespace BGP
{
class BgpProcess;

class DecisionEngine
{
public:
    explicit DecisionEngine(BgpProcess& p)
        : proc(p), comparator(p) {}

    template <typename N>
    std::optional<LocalRoute<N>> selectBest(std::vector<InboundRoute<N>*>& canidates, size_t maxEPaths, size_t maxIPaths) const
    {
        if (canidates.empty())
            return std::nullopt;

        static const IPAddress kEmpty{};
        auto nbrAddr = [](const InboundRoute<N>* r) -> const IPAddress& {
            return r->sourceNeighbor ? r->sourceNeighbor->globalNbr().neighborAddress : kEmpty;
        };

        InboundRoute<N>* best = canidates.front();
        for (size_t i = 1; i < canidates.size(); i++)
        {
            InboundRoute<N>* cand = canidates[i];
            if (comparator.better(*cand, nbrAddr(cand), *best, nbrAddr(best)))
                best = cand;
        }

        LocalRoute<N> result{*best, {}};

        size_t maxPaths = best->ebgp
            ? maxEPaths : maxIPaths;

        if (maxPaths > 1)
        {
            const IPAddress& bestNbr = nbrAddr(best);
            for (InboundRoute<N>* cand : canidates)
            {
                if (cand == best)
                    continue;
                const IPAddress& candNbr = nbrAddr(cand);
                if (!comparator.better(*best, bestNbr, *cand, candNbr) &&
                    !comparator.better(*cand, candNbr, *best, bestNbr))
                {
                    result.multipaths.push_back(cand);
                    if (result.multipaths.size() + 1 >= maxPaths)
                        break;
                }
            }
        }

        return result;
    }

    bool equivalent(const InboundRouteBase& lhs, const IPAddress& lhsNbr, const InboundRouteBase& rhs, const IPAddress& rhsNbr) const
    {
        if (comparator.better(lhs, lhsNbr, rhs, rhsNbr))
            return false;
        if (comparator.better(rhs, rhsNbr, lhs, lhsNbr))
            return false;
        return true;
    }

private:
    BgpProcess& proc;
    BestPathComparator comparator;
};
}

#endif // BGP_DECISION_ENGINE_H
