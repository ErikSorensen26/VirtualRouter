/**
 * @file DecisionEngine.hpp
 * @brief Decision engine: best path computation and route installation.
 */

/**
 * @defgroup BGP_DECISION BGP Decision
 * @ingroup BGP
 * @brief Best-path selection algorithm and decision engine.
 */

#ifndef BGP_DECISION_ENGINE_H
#define BGP_DECISION_ENGINE_H

#include <algorithm>
#include <optional>
#include "BestPath.h"

namespace routing::bgp
{
class BgpProcess;

/**
 * @brief Best path selection and route installation for one BGP process.
 * @ingroup BGP_DECISION
 *
 * Implements the decision process (RFC 4271 § 9.1): takes candidate routes
 * from Adj-RIB-In, selects best using BestPathComparator, manages equal-cost
 * multipaths, and installs results to Loc-RIB and routing table.
 *
 * ## Lifecycle
 * Owned by BgpProcess. Created during process initialization and destroyed
 * with the process.
 *
 * ## Concurrency Model
 * Thread-safe if BgpProcess is single-threaded (scheduler pattern).
 *
 * @tparam N NLRI type (IPv4Prefix, IPv6Prefix, etc.)
 * @see BestPathComparator, AddressFamilyInstance
 */
class DecisionEngine
{
public:
    explicit DecisionEngine(BgpProcess& p, BestPathConfig cfg = {})
        : proc(p), comparator(p, cfg) {}

    template <typename N>
    std::optional<LocalRoute<N>> selectBest(std::vector<InboundRoute<N>*>& canidates, size_t maxEPaths, size_t maxIPaths) const
    {
        if (canidates.empty())
            return std::nullopt;

        static const types::IPAddress kEmpty{};
        auto nbrAddr = [](const InboundRoute<N>* r) -> const types::IPAddress& {
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
            const types::IPAddress& bestNbr = nbrAddr(best);
            for (InboundRoute<N>* cand : canidates)
            {
                if (cand == best)
                    continue;
                const types::IPAddress& candNbr = nbrAddr(cand);
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

    // Returns all candidates sorted best-first (stable, does not modify input).
    template <typename N>
    std::vector<InboundRoute<N>*> rankCandidates(std::vector<InboundRoute<N>*> candidates) const
    {
        static const types::IPAddress kEmpty{};
        auto nbrAddr = [](const InboundRoute<N>* r) -> const types::IPAddress& {
            return r->sourceNeighbor ? r->sourceNeighbor->globalNbr().neighborAddress : kEmpty;
        };

        std::stable_sort(candidates.begin(), candidates.end(),
            [&](const InboundRoute<N>* a, const InboundRoute<N>* b) {
                return comparator.better(*a, nbrAddr(a), *b, nbrAddr(b));
            });

        return candidates;
    }

    bool equivalent(const InboundRouteBase& lhs, const types::IPAddress& lhsNbr, const InboundRouteBase& rhs, const types::IPAddress& rhsNbr) const
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
} // namespace routing

#endif // BGP_DECISION_ENGINE_H

