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
#include "bgp/neighbor/NeighborAf.h"
#include "bgp/attributes/AttributeManager.hpp"

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
    static uint32_t getRid(BgpProcess& proc);
    static Config::BgpRegistry& getConfigs(BgpProcess& proc);
    static AttributeManager& getAttrMgr(BgpProcess& proc);
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

    void refreshPeer(Session& session)
    {
        const uint32_t peerRid = session.getPeerRid();
        auto outIt = adjRibOut.find(peerRid);
        if (outIt == adjRibOut.end())
            return;

        BuildUpdate<NlriT> update;

        for (auto& [nlri, route] : outIt->second)
        {
            // TODO: group by pathId
            auto egressAttrs = applyEgressPolicy(route, session);
            if (!egressAttrs.has_value())
                continue;

            typename BuildUpdate<NlriT>::Announcement ann;
            ann.attrs = *egressAttrs;
            ann.nlri.push_back(nlri);
            update.announcements.push_back(std::move(ann));
        }

        if (!update.announcements.empty())
            session.sendUpdate<N>(update);
    }

    void invalidatePeer(uint32_t peer)
    {
        auto out = adjRibOut.find(peer);
        if (out != adjRibOut.end())
            adjRibOut.erase(out);

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

        PerPeerInTable<NlriT>& peerIn = adjRibIn[peer.rid];

        std::vector<NlriT> touched;
        touched.reserve(update.announcements.size() + update.withdrawn.size());

        // Nodes kept to keep references valid in the locRib while routes recalculate
        std::vector<typename PerPeerInTable<NlriT>::node_type> withdraws;
        withdraws.reserve(update.withdrawn.size());

        for (const auto& n : update.withdrawn)
        {
            auto it = peerIn.find(n);
            if (it != peerIn.end())
                withdraws.push_back(std::move(peerIn.extract(n)));
            touched.push_back(n);
        }

        if (update.attrs.has_value())
        {
            auto& attrMgr = AddressFamilyInstanceHelper::getAttrMgr(process);

            // Obtain REMOTE_AS from session
            auto& remAs = peer.getConfigs().get<Config::BgpNeighborSession::REMOTE_AS>();
            const uint32_t peerAs = remAs.hasValue() ? remAs.load() : 0;

            const uint32_t localAs = AddressFamilyInstanceHelper::getAsNum(process);
            const bool isEbgp = (peerAs != 0) && (peerAs != localAs);

            for (const auto& n : update.announcements)
            {
                InboundRoute<NlriT> r(attrMgr);
                r.nlri = n;
                r.neighborRouterId = peer.rid;
                r.peerAs = peerAs;
                r.ebgp = isEbgp;
                r.igpCost = resolveIgpMetric(r.path.nextHop);

                if (!applyIngressPolicy(r, peer))
                {
                    auto it = peerIn.find(n);
                    if (it != peerIn.end())
                        peerIn.erase(it);

                    touched.push_back(n);
                    continue;
                }

                peerIn[n] = std::move(r);
                touched.push_back(n);
            }
        }

        for (const auto& n : touched)
            recomputeNlri(n);
    }

    void recomputeNlri(const typename N::Nlri& nlri)
    {
        std::vector<LocalRoute<N>> canidates;
        canidates.reserve(adjRibIn.size());

        for (auto& [peer, peerTable] : adjRibIn)
        {
            auto it = peerTable.find(nlri);
            if (it == peerTable.end())
                continue;

            it->second.igpCost = resolveIgpMetric(it->second.path.nextHop);
            canidates.push_back(it->second);
        }

        std::optional<LocalRoute<NlriT>> best;

        if (canidates.empty())
        {
            const bool alwaysCompareMed =
                AddressFamilyInstanceHelper::getConfigs(process).get<Config::Bgp::BGP_ALWAYS_COMPARE_MED>().load();

            DecisionEngine decision(BestPathOptions{
                .alwaysCompareMed = alwaysCompareMed
            });

            best = decision.selectBest(canidates);
        }

        auto lit = locRib.find(nlri);
        const bool hadBest = (lit != locRib.end());

        if (!best.has_value())
        {
            if (hadBest)
            {
                withdrawFromRib(nlri);
                locRib.erase(lit);
                recomputeAdjRibOut(nlri, std::nullopt);
            }
            return;
        }

        if (!hadBest)
        {
            locRib.empty(nlri, *best);
            installToRib(best.value());
            recomputeAdjRibOut(nlri, best);
        }
    }

    void installToRib(const InboundRoute<NlriT>& route)
    {
        policy.installRoute(route);
    }

    void withdrawFromRib(const NlriT& nlri)
    {
        policy.withdrawRoute(nlri);
    }

    // Returns true if the route should be dropped (filtered out) before entering adj-rib-in.
    // Add ingress route-map / prefix-list / community filter logic here.
    bool applyIngressPolicy(InboundRoute<NlriT>& route, const PathAttribute& pathAttrs, const Neighbor& peer)
    {
        // TODO: drop logic

        // TODO: ingress policy (route-maps, prefix-lists, community filters, etc.)

        // TODO: update attr manager and fill in pathId

        return false; // false = accept
    }

    // Returns nullopt if the route should be suppressed for this peer (outbound policy drop).
    // Returns the (possibly modified) PathAttribute to advertise otherwise.
    // Add outbound route-map / prefix-list / community filter logic here.
    std::optional<uint32_t> applyEgressPolicy(const LocalRoute<NlriT>& route, const Session& session)
    {
        const NeighborAf& localNeighbor = session.getNeighbor().getAfNeighbor(family);
        auto& configs = localNeighbor.getConfigs();


        PathAttribute& pathAttr = route.getPathAttributes();

        Path path = pathAttr.path;
        Attributes attrs = pathAttr.attrs;

        const uint32_t localAs = AddressFamilyInstanceHelper::getAsNum(process);

        if (session.isEbgp())
        {
            // Handle NextHopSelf
            if (!configs.get<Config::BgpNeighbor::NEXT_HOP_UNCHANGED>().load() ||
                configs.get<Config::BgpNeighbor::NEXT_HOP_SELF_ALL>().load())
                path.nextHop = session.getNeighbor().neighborAddress;
            attrs.localPref = std::nullopt;

            // Prepend own AS to AS_PATH (RFC 4271 §5.1.2)
            AsPathSegment seg;
            seg.segmentType = BGP_AS_SEQUENCE;
            seg.asns.push_back(localAs);
            attrs.asPath.insert(attrs.asPath.begin(), std::move(seg));
        }
        else
        {
            if ((configs.get<Config::BgpNeighbor::NEXT_HOP_SELF>().load() &&
                 route.in.neighborRouterId != AddressFamilyInstanceHelper::getRid(process)) ||
                configs.get<Config::BgpNeighbor::NEXT_HOP_SELF_ALL>().load())
                path.nextHop = session.getPrimaryConnection()->socketKey()->local.address;
        }

        // TODO: outbound route-map / prefix-list / community filter / MED setting etc.

        // Update attr table 
        AttributeManager& attrMgr = AddressFamilyInstanceHelper::getAttrMgr(process);
        return attrMgr.acquire(attrs, path);
    }

    void recomputeAdjRibOut(const NlriT& nlri, const std::optional<LocalRoute<NlriT>>& best)
    {
        auto& ntable = AddressFamilyInstanceHelper::getNtable(process);
        ntable.forEachPeer([&](Neighbor& nbr) {
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
            {
                const bool targetIsClient = nbr.getAfNeighbor(family).getConfigs()
                    .get<Config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>().load();

                const bool fromIbgp = !best->ebgp;
                const bool toIbgp = !session->isEbgp();

                if (fromIbgp && toIbgp)
                {
                    NeighborAf& srcNbr = best.value().sourceNeighbor;
                    const bool senderIsClient = srcNbr.getConfigs()
                        .get<Config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>().load();

                    // Route from NON-CLIENT, cannot send to NON-CLIENT
                    if (!senderIsClient && !targetIsClient)
                    {
                        withdrawFromPeer();
                        return;
                    }

                    // Do not reflect back to sender
                    if (best->sourceNeighbor.globalNbr() == &nbr)
                    {
                        withdrawFromPeer();
                        return;
                    }
                }
            }

            auto attrId = applyEgressPolicy(*best, *session);
            if (!attrId.has_value())
            {
                withdrawFromPeer();
                return;
            }

            BuildUpdate<NlriT> update;
            typename BuildUpdate<NlriT>::Announcement ann;
            ann.attrs = *AddressFamilyInstanceHelper::getAttrMgr(process).getAttributes(attrId);
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
