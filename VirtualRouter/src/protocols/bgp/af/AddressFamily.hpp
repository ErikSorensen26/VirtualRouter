// AddressFamily.h

#ifndef BGP_ADDRESS_FAMILY_HPP
#define BGP_ADDRESS_FAMILY_HPP

#include <optional>
#include <functional>
#include <IPAddress.hpp>

#include "bgp/BgpProcess.h"
#include "bgp/BgpTypes.hpp"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/session/Session.h"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/decision/DecisionEngine.hpp"

namespace BGP
{
template <typename N>
class AddressFamily
{
public:
    using IgpMetricResolver = std::function<uint64_t(const IPAddress&)>;

    AddressFamily(BgpProcess& proc, AfiSafi fam)
        : process(proc),
          family(fam),
          nlriInfo(*proc.routingInstance),
          igpMetricResolver([](const IPAddress&) { return std::numeric_limits<uint64_t>::max(); })
    {}

    AddressFamily(const AddressFamily&) = delete;
    AddressFamily& operator=(const AddressFamily&) = delete;
    AddressFamily(AddressFamily&&) = delete;
    AddressFamily& operator=(AddressFamily&&) = delete;

public:
    const AfiSafi& getFamily() const noexcept { return family; }
    
    void setIgpMetricResolver(IgpMetricResolver resolver)
    {
        igpMetricResolver = std::move(resolver);
    }

    void onUpdateFromPeer(Neighbor& peer, ParsedUpdate<typename N::Nlri>& update, const std::optional<IPAddress>& nextHopOverride)
    {
        if (!process.getNtable().lookup(peer.rid)) return;

        PerPeerAdjTable<typename N::Nlri>& peerIn = adjRibIn[peer];

        std::vector<typename N::Nlri> touched;
        touched.reserve(update.announced.size() + update.withdrawn.size());

        for (const auto& n : update.withdrawn)
        {
            auto it = peerIn.find(n);
            if (it != peerIn.end())
                peerIn.erase(it);
            touched.push_back(n);
        }

        for (const auto& [n, attr] : update.announced)
        {
            RouteCanidate<typename N::Nlri> r{};
            r.nlri = n;
            r.attributes = attr;

            if (nextHopOverride.has_value())
                r.nextHop = *nextHopOverride;
            else
                r.nextHop = r.attributes.nextHop;

            r.neighborRouterId = peer.rid;
            r.neighborAddress = peer.neighborAddress;

            r.peerAs = peer.getConfigs().get<Config::BgpNeighbor::REMOTE_AS>().load();
            r.ebgp = r.peerAs != process.asNumber;

            r.igpCost = resolveIgpMetric(r.nextHop);

            peerIn[n] = std::move(r);
            touched.push_back(n);
        }

        for (const auto& n : touched)
            recomputeNlri(n);
    }

    void invalidatePeer(uint32_t peer)
    {
        auto it = adjRibIn.find(peer);
        if (it == adjRibIn.end())
            return;

        // Copy keys first because recompute can mutate them
        std::vector<typename N::Nlri> keys;
        keys.reserve(it->second.size());
        for (const auto& [nlri, _] : it.second)
            keys.push_back(nlri);

        adjRibIn.erase(it);

        for (const auto& n : keys)
            recomputeNlri(n);
    }

private:
    void recomputeNlri(const typename N::Nlri& nlri)
    {
        std::vector<RouteCanidate<typename N::Nlri>*> canidates;
        canidates.reserve(adjRibIn.size());

        for (auto& [peer, peerTable] : adjRibIn)
        {
            auto it = peerTable.find(nlri);
            if (it == peerTable.end())
                continue;

            it.second.igpCost = resolveIgpMetric(it->second.nextHop);
            canidates.push_back(&it->second);
        }

        const bool alwaysCompareMed =
            process.getConfigs().get<Config::Bgp::BGP_ALWAYS_COMPARE_MED>().load();

        DecisionEngine decision(BestPathOptions{.alwaysCompareMed = alwaysCompareMed});
        std::optional<RouteCanidate<typename N::Nlri>> best;

        if (!canidates.empty())
            best = decision.selectBest(canidates);

        auto lit = locRib.find(nlri);
        const bool had = (lit != locRib.end());

        if (!best.has_value())
        {
            if (had)
            {
                locRib.erase(lit);
                withdrawLocRibRoute(nlri);
                recomputeAdjRibOut(nlri, std::nullopt);
            }
            return;
        }

        if (!had || lit->second != best.value())
        {
            locRib[nlri] = best.value();
            installLocRibRoute(best.value());
            recomputeAdjRibOut(nlri, best);
        }
    }

    void installLocRibRoute(const RouteCanidate<typename N::Nlri>& route)
    {
        nlriInfo.installRoute(route);
    }

    void withdrawLocRibRoute(const N& nlri)
    {
        nlriInfo.withdrawRoute(nlri);
    }

    void recomputeAdjRibOut(const N& nlri, const std::optional<RouteCanidate<N>>& best)
    {
        auto& sessions = process.getSessions();

        for (auto& [_, session] : sessions)
        {
            if (!session.established())
                continue;

            const uint32_t peer = session.getRid();
            auto& peerOut = adjRibOut[peer];
            const bool had = (peerOut.find(nlri) != peerOut.end());

            auto withdrawnFromPeer = [&]()
            {
                if (!had) return;

                ParsedUpdate<N> withdraw;
                buildWithdrawUpdate(withdraw, nlri);

                process.getTransmission().sendUpdate(session, withdraw);
                peerOut.erase(nlri);
            };

            if (!best.has_value())
            {
                withdrawnFromPeer();
                continue;
            }

            const bool familyNegotiated = session.capabilities().families.contains(family);
            if (!familyNegotiated)
            {
                withdrawnFromPeer();
                continue;
            }
        }
    }

    void rebuildMergedAdjRibForNlri(uint32_t rid, const N& nlri)
    {
        auto& inIt = adjRibIn.find(rid);
        if (inIt == adjRibIn.end())
            return;

        RouteCanidate<N>& dst = inIt->second[nlri];

        for ()
    }

    uint64_t resolveIgpMetric(const IPAddress& nextHop) const
    {
        if (!igpMetricResolver)
            return std::numeric_limits<uint64_t>::max();
        return igpMetricResolver(nextHop);
    }

private:
    AdjRibInTable<N> adjRibIn;
    LocRibTable<N> locRib;
    AdjRibOutTable<N> adjRibOut;

    N nlriInfo;

    IgpMetricResolver igpMetricResolver;

    BgpProcess& process;
    AfiSafi family;
};
}

#endif // BGP_ADDRESS_FAMILY_HPP
