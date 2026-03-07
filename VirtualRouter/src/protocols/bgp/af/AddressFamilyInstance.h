// AddressFamilyInstance.h

#ifndef BGP_ADDRESS_FAMILY_INSTANCE_H
#define BGP_ADDRESS_FAMILY_INSTANCE_H

#include <functional>
#include <IPAddress.hpp>
#include <VirtualRouter.h>

#include "bgp/BgpTypes.hpp"
#include "bgp/session/Session.h"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/transport/BgpRx.h"
#include "bgp/transport/BgpTx.h"
#include "bgp/decision/DecisionEngine.hpp"
#include "bgp/neighbor/NeighborTable.h"

namespace BGP
{
// Definition of Session::sendUpdate<N> here, after both Session.h and BgpTx.h are visible
template <typename N>
inline void Session::sendUpdate(const BuildUpdate<typename N::Nlri>& update)
{
    if (primaryConn)
    {
        BgpTx::buildUpdate<N>(*primaryConn, *this, update);
        primaryConn->flush();
    }
}
}

namespace BGP
{
class AddressFamilyInstanceHelper
{
private:
    template <typename T>
    friend class AddressFamilyInstance;

    static VirtualRouter& getRoutingInstance(BgpProcess& proc);
    static NeighborTable& getNtable(BgpProcess& proc);
    static uint32_t getAsNum(BgpProcess& proc);
    static Config::BgpRegistry& getConfigs(BgpProcess& proc);
};


/**
 * class AddressFamilyInstance<N>
 *
 * N is a concrete subclass of AddressFamilyPolicy<SomeNlriType>.
 * It provides:
 *   - N::Nlri      — the NLRI type (e.g. IPPrefix)
 *   - N::installRoute(RouteCandidate<Nlri>&) — install into the routing table
 *   - N::withdrawRoute(const Nlri&)          — remove from the routing table
 *
 * Each AddressFamilyInstance<N> instance manages one AFI/SAFI:
 *   - Adj-RIB-In  (per peer, per prefix)
 *   - Loc-RIB     (per prefix, best route)
 *   - Adj-RIB-Out (per peer, per prefix, post-policy)
 */
template <typename N>
class AddressFamilyInstance
{
public:
    static constexpr AfiSafi afi = N::afi;
    using NlriT = typename N::Nlri;
    using IgpMetricResolver = std::function<uint64_t(const IPAddress&)>;

    AddressFamilyInstance(BgpProcess& proc, AfiSafi fam)
        : process(proc),
          family(fam),
          policy(AddressFamilyInstanceHelper::getRoutingInstance(proc)),
          igpMetricResolver([](const IPAddress&)
              { return std::numeric_limits<uint64_t>::max(); })
    {}

    AddressFamilyInstance(const AddressFamilyInstance&) = delete;
    AddressFamilyInstance& operator=(const AddressFamilyInstance&) = delete;
    AddressFamilyInstance(AddressFamilyInstance&&) = delete;
    AddressFamilyInstance& operator=(AddressFamilyInstance&&) = delete;

    const AfiSafi& getFamily() const noexcept { return family; }
    
    void setIgpMetricResolver(IgpMetricResolver resolver)
    {
        igpMetricResolver = std::move(resolver);
    }

    bool onUpdateFromPeer(Session& session, IncomingUpdate& uinfo, Notification& error)
    {
        ParsedUpdate<NlriT> update;

        if (!BgpRx::processUpdate<N>(session, uinfo, update, error))
            return false;

        onParsedUpdateFromPeer(session.getNeighbor(), update);

        return true;
    }

    void invalidatePeer(uint32_t peer)
    {
        auto it = adjRibIn.find(peer);
        if (it == adjRibIn.end())
            return;

        // Copy keys first because recompute can mutate them
        std::vector<typename N::Nlri> keys;
        keys.reserve(it->second.size());
        for (const auto& [nlri, _] : it->second)
            keys.push_back(nlri);

        adjRibIn.erase(it);

        for (const auto& n : keys)
            recomputeNlri(n);
    }

private:
    void onParsedUpdateFromPeer(Neighbor& peer, ParsedUpdate<typename N::Nlri>& update)
    {
        if (!AddressFamilyInstanceHelper::getNtable(process).lookup(peer.rid))
            return;

        PerPeerAdjTable<NlriT>& peerIn = adjRibIn[peer.rid];

        std::vector<NlriT> touched;
        touched.reserve(update.announcements.size() + update.withdrawn.size());

        for (const auto& n : update.withdrawn)
        {
            auto it = peerIn.find(n);
            if (it != peerIn.end())
                peerIn.erase(it);
            touched.push_back(n);
        }

        if (update.attrs.has_value())
        {
            // Obtain REMOTE_AS from session
            auto& remAs = peer.getConfigs().get<Config::BgpNeighborSession::REMOTE_AS>();
            const uint32_t peerAs = remAs.hasValue() ? remAs.load() : 0;
            const bool isEbgp = (peerAs != 0) && (peerAs != AddressFamilyInstanceHelper::getAsNum(process));

            for (const auto& n : update.announcements)
            {
                RouteCanidate<NlriT> r{};
                r.nlri = n;
                r.attrs = update.attrs->attrs;
                r.path = update.attrs->path;

                r.neighborRouterId = peer.rid;
                r.neighborAddress = peer.neighborAddress;
                r.peerAs = peerAs;
                r.ebgp = isEbgp;
                r.igpCost = resolveIgpMetric(r.path.nextHop);

                peerIn[n] = std::move(r);
                touched.push_back(n);
            }
        }

        for (const auto& n : touched)
            recomputeNlri(n);
    }

    void recomputeNlri(const typename N::Nlri& nlri)
    {
        std::vector<RouteCanidate<NlriT>> canidates;
        canidates.reserve(adjRibIn.size());

        for (auto& [peer, peerTable] : adjRibIn)
        {
            auto it = peerTable.find(nlri);
            if (it == peerTable.end())
                continue;

            it->second.igpCost = resolveIgpMetric(it->second.path.nextHop);
            canidates.push_back(it->second);
        }

        const bool alwaysCompareMed =
            AddressFamilyInstanceHelper::getConfigs(process).get<Config::Bgp::BGP_ALWAYS_COMPARE_MED>().load();

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
                withdrawFromLocRib(nlri);
                locRib.erase(lit);
                recomputeAdjRibOut(nlri, std::nullopt);
            }
            return;
        }

        if (!had || lit->second != best.value())
        {
            locRib[nlri] = best.value();
            installToLocRib(best.value());
            recomputeAdjRibOut(nlri, best);
        }
    }

    void installToLocRib(RouteCanidate<NlriT>& route)
    {
        policy.installRoute(route);
    }

    void withdrawFromLocRib(const NlriT& nlri)
    {
        policy.withdrawRoute(nlri);
    }

    void recomputeAdjRibOut(const NlriT& nlri, const std::optional<RouteCanidate<NlriT>>& best)
    {
        AddressFamilyInstanceHelper::getNtable(process).forEachNeighbor([&](Neighbor& nbr) {
            Session* session = nbr.session;
            if (!session || !session->established())
                return;

            const uint32_t peerRid = session->getPeerRid();
            auto& peerOut = adjRibOut[peerRid];
            const bool had = (peerOut.find(nlri) != peerOut.end());

            auto withdrawFromPeer = [&]()
            {
                if (!had)
                    return;
                BuildUpdate<NlriT> withdraw;
                withdraw.withdrawn.push_back(nlri);
                session->sendUpdate<N>(withdraw);
                peerOut.erase(nlri);
            };

            if (!best.has_value())
            {
                withdrawFromPeer();
                return;
            }

            // Check that this AFI/SAFI was negotiated with this peer
            const auto& negotiated = session->getNegotiated();
            const bool familyOk = std::any_of(
                negotiated.activeFamilies.begin(),
                negotiated.activeFamilies.end(),
                [&](const AfiSafi& f) { return f == family; });

            if (!familyOk)
            {
                withdrawFromPeer();
                return;
            }

            // Don't reflect iBGP routes back to iBGP peers (no route reflection)
            const bool fromIbgp = !best->ebgp;
            const bool toIbgp = !session->isEbgp();
            if (fromIbgp && toIbgp)
                return;

            BuildUpdate<NlriT> update;
            typename BuildUpdate<NlriT>::Announcement ann;
            ann.attrs = PathAttribute{best->attrs, best->path};
            ann.nlri.push_back(nlri);
            update.announcements.push_back(std::move(ann));
            session->sendUpdate<N>(update);
            peerOut[nlri] = *best;
        });
    }

    uint64_t resolveIgpMetric(const IPAddress& nextHop) const
    {
        if (!igpMetricResolver)
            return std::numeric_limits<uint64_t>::max();
        return igpMetricResolver(nextHop);
    }

private:
    AdjRibInTable<NlriT> adjRibIn;
    LocRibTable<NlriT> locRib;
    AdjRibOutTable<NlriT> adjRibOut;

    N policy;

    IgpMetricResolver igpMetricResolver;

    BgpProcess& process;
    AfiSafi family;
};
}

#endif // BGP_ADDRESS_FAMILY_H
