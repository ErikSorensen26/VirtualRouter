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
 * N is a concrete subclass of NlriPolicy<SomeNlriType, AfiSafi>.
 * It provides:
 *   - N::Nlri      — the NLRI type (e.g. IPPrefix)
 *   - N::installRoute(LocalRoute<Nlri>&) — install into the routing table
 *   - N::withdrawRoute(const Nlri&)      — remove from the routing table
 *
 * Each AddressFamilyInstance<N> instance manages one AFI/SAFI:
 *   - Adj-RIB-In  (per peer, per prefix) — InboundRoute<Nlri>
 *   - Loc-RIB     (per prefix, best route) — LocalRoute<Nlri> pointing into Adj-RIB-In
 *   - Adj-RIB-Out (per peer, per prefix, post-egress-policy) — OutboundRoute<Nlri>
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
              { return std::numeric_limits<uint64_t>::max(); }),
          configs([&proc, &fam]() {
              auto& bgpConfigs = AddressFamilyInstanceHelper::getConfigs(proc);
              auto& vrf = AddressFamilyInstanceHelper::getRoutingInstance(proc);
              auto& registry = vrf.getRegistry();
              return registry.emplaceBack(
                  bgpConfigs.get<Config::Bgp::ADDRESS_FAMILIES>(), fam.flatten(),
                  Config::generateBgpAfKey(vrf.getInstanceId(), fam.afi, fam.safi)
              );
          })
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

        auto& attrMgr = AddressFamilyInstanceHelper::getAttrMgr(process);

        // Group NLRIs by their stored egress pathId to batch into one Announcement per path.
        std::unordered_map<uint32_t, size_t> pathToAnn;
        BuildUpdate<NlriT> update;

        for (auto& [nlri, outRoute] : outIt->second)
        {
            if (!outRoute.pathId.has_value())
                continue;

            PathAttribute pa = attrMgr.get(*outRoute.pathId);

            auto ait = pathToAnn.find(*outRoute.pathId);
            if (ait == pathToAnn.end())
            {
                pathToAnn[*outRoute.pathId] = update.announcements.size();
                typename BuildUpdate<NlriT>::Announcement ann;
                ann.attrs = std::move(pa);
                ann.nlri.push_back(nlri);
                update.announcements.push_back(std::move(ann));
            }
            else
            {
                update.announcements[ait->second].nlri.push_back(nlri);
            }
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

        std::vector<NlriT> keys;
        keys.reserve(it->second.size());
        for (const auto& [nlri, inRoute] : it->second)
        {
            keys.push_back(nlri);
            auto lit = locRib.find(nlri);
            if (lit != locRib.end() && &lit->second.in == &inRoute)
                locRib.erase(lit);
        }

        adjRibIn.erase(it);

        for (const auto& n : keys)
            recomputeNlri(n);
    }

private:
    void onParsedUpdateFromPeer(Neighbor& peer, ParsedUpdate<NlriT>& update)
    {
        Neighbor* nbr = AddressFamilyInstanceHelper::getNtable(process).lookup(peer.rid);
        if (!nbr) return;

        PerPeerInTable<NlriT>& peerIn = adjRibIn[peer.rid];

        std::vector<NlriT> touched;
        touched.reserve(update.announcements.size() + update.withdrawn.size());

        for (const auto& n : update.withdrawn)
        {
            auto it = peerIn.find(n);
            if (it != peerIn.end())
            {
                auto lit = locRib.find(n);
                if (lit != locRib.end() && lit->second.in == &it->second)
                    locRib.erase(lit);
                peerIn.erase(it);
            }
            touched.push_back(n);
        }

        if (update.attrs.has_value())
        {
            auto& attrMgr = AddressFamilyInstanceHelper::getAttrMgr(process);

            auto& remAs = peer.getConfigs().get<Config::BgpNeighborSession::REMOTE_AS>();
            const uint32_t peerAs = remAs.hasValue() ? remAs.load() : 0;
            const uint32_t localAs = AddressFamilyInstanceHelper::getAsNum(process);
            const bool isEbgp = (peerAs != 0) && (peerAs != localAs);

            uint32_t pid = attrMgr.acquire(update.attrs->attrs, update.attrs->path);

            NeighborAf& naf = peer.getAfNeighbor(family);

            bool first = true;
            for (const auto& n : update.announcements)
            {
                if (!first)
                    attrMgr.retain(pid);
                first = false;

                InboundRoute<NlriT> r(attrMgr, pid, n, &naf);
                r.neighborRouterId = peer.rid;
                r.peerAs            = peerAs;
                r.ebgp              = isEbgp;
                r.igpCost           = resolveIgpMetric(update.attrs->path.nextHop);

                auto& weight = nbr->getAfNeighbor(family).getConfigs().get<Config::BgpNeighbor::WEIGHT>();
                if (weight.hasValue()) r.weigth = weight.load();

                if (applyIngressPolicy(r))
                {
                    continue;
                }

                // Nullify before erase in case this entry is the current locRib winner.
                {
                    auto existing = peerIn.find(n);
                    if (existing != peerIn.end())
                    {
                        auto lit = locRib.find(n);
                        if (lit != locRib.end() && lit->second.in == &existing->second)
                            lit->second.in = nullptr;
                        peerIn.erase(existing);
                    }
                }
                peerIn.emplace(n, std::move(r));
                touched.push_back(n);
            }
        }

        for (const auto& n : touched)
            recomputeNlri(n);
    }

    void recomputeNlri(const NlriT& nlri)
    {
        std::vector<InboundRoute<NlriT>*> candidates;
        candidates.reserve(adjRibIn.size());

        for (auto& [peer, peerTable] : adjRibIn)
        {
            auto it = peerTable.find(nlri);
            if (it == peerTable.end())
                continue;

            // Refresh IGP cost before running best-path.
            auto attrs = it->second.getPathAttributes();
            if (attrs.has_value())
                it->second.igpCost = resolveIgpMetric(attrs->path.nextHop);

            candidates.push_back(&it->second);
        }

        DecisionEngine decision(process);
        std::optional<LocalRoute<NlriT>> best = decision.selectBest(
            candidates,
            configs->get<Config::BgpAddressFamily::MAXIMUM_PATHS_EBGP>().load(),
            configs->get<Config::BgpAddressFamily::MAXIMUM_PATHS_IBGP>().load()
        );

        auto lit = locRib.find(nlri);
        const bool had = (lit != locRib.end());

        if (!best.has_value())
        {
            if (had)
            {
                withdrawFromRib(nlri);
                locRib.erase(lit);
                recomputeAdjRibOut(nlri, nullptr);
            }
            return;
        }

        if (!had || lit->second.in != best.in)
        {
            if (had)
                locRib.erase(lit);

            locRib.emplace(nlri, *best);
            installToRib(locRib.at(nlri));
            recomputeAdjRibOut(nlri, best.in);
        }
    }

    void installToRib(LocalRoute<NlriT>& route)
    {
        policy.installRoute(route);
    }

    void withdrawFromRib(const NlriT& nlri)
    {
        policy.withdrawRoute(nlri);
    }

    // Returns true if the route should be dropped before entering Adj-RIB-In.
    // Add ingress route-map / prefix-list / community filter logic here.
    bool applyIngressPolicy(const InboundRoute<NlriT>& /*route*/)
    {
        // TODO: decide if dropped
        // TODO: ingress policy (route-maps, prefix-lists, community filters, etc.)
        return false; // false = accept
    }

    // Returns nullopt if the route should be suppressed for this peer (outbound drop).
    // Returns the (possibly modified) PathAttribute to advertise otherwise.
    // Add outbound route-map / prefix-list / community filter logic here.
    std::optional<PathAttribute> applyEgressPolicy(const InboundRoute<NlriT>& route, const Session& session)
    {
        auto pa_opt = route.getPathAttributes();
        if (!pa_opt.has_value())
            return std::nullopt;

        PathAttribute pa = *pa_opt;

        const NeighborAf& localNeighbor = session.getNeighbor().getAfNeighbor(family);
        const auto& cfgs = localNeighbor.getConfigs();

        if (session.isEbgp())
        {
            pa.attrs.localPref = std::nullopt;

            // Prepend own AS to AS_PATH
            const uint32_t localAs = AddressFamilyInstanceHelper::getAsNum(process);
            AsPathSegment seg;
            seg.segmentType = BGP_AS_SEQUENCE;
            seg.asns.push_back(localAs);
            pa.attrs.asPath.insert(pa.attrs.asPath.begin(), std::move(seg));

            // next-hop-self for eBGP
            if (!cfgs.get<Config::BgpNeighbor::NEXT_HOP_UNCHANGED>().load() ||
                cfgs.get<Config::BgpNeighbor::NEXT_HOP_SELF_ALL>().load())
                pa.path.nextHop = session.getNeighbor().neighborAddress;
        }
        else
        {
            // next-hop-self for iBGP
            if ((cfgs.get<Config::BgpNeighbor::NEXT_HOP_SELF>().load() &&
                 route.neighborRouterId != AddressFamilyInstanceHelper::getRid(process)) ||
                cfgs.get<Config::BgpNeighbor::NEXT_HOP_SELF_ALL>().load())
                pa.path.nextHop = session.getPrimaryConnection()->socketKey()->local.address;
        }

        // TODO: outbound route-map / prefix-list / community filter / MED setting etc.

        return pa;
    }

    void recomputeAdjRibOut(const NlriT& nlri, InboundRoute<NlriT>* best)
    {
        auto& attrMgr = AddressFamilyInstanceHelper::getAttrMgr(process);

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

            if (!best)
            {
                withdrawFromPeer();
                return;
            }

            // Check that this AFI/SAFI was negotiated with this peer.
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

            // iBGP split-horizon with route-reflector awareness.
            const bool fromIbgp = !best->ebgp;
            const bool toIbgp   = !session->isEbgp();
            if (fromIbgp && toIbgp)
            {
                const bool targetIsClient = nbr.getAfNeighbor(family).getConfigs()
                    .get<Config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>().load();

                const bool senderIsClient = best->sourceNeighbor.getConfigs()
                    .template get<Config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>().load();

                // Non-client route cannot be reflected to a non-client peer.
                if (!senderIsClient && !targetIsClient)
                {
                    withdrawFromPeer();
                    return;
                }

                // Do not reflect back to the originating neighbor.
                if (&best->sourceNeighbor.globalNbr() == &nbr)
                {
                    withdrawFromPeer();
                    return;
                }
            }

            auto egressAttrs = applyEgressPolicy(*best, *session);
            if (!egressAttrs.has_value())
            {
                withdrawFromPeer();
                return;
            }

            uint32_t oPid = attrMgr.acquire(egressAttrs->attrs, egressAttrs->path);

            // Skip sending if the egress path is identical to what we already sent.
            auto existing = peerOut.find(nlri);
            if (existing != peerOut.end() && existing->second.pathId == oPid)
            {
                attrMgr.release(oPid);
                return;
            }

            // Replace the outbound entry (destructor releases the old pathId).
            peerOut.erase(nlri);
            peerOut.emplace(std::piecewise_construct,
                std::forward_as_tuple(nlri),
                std::forward_as_tuple(attrMgr, oPid, nlri));

            BuildUpdate<NlriT> update;
            typename BuildUpdate<NlriT>::Announcement ann;
            ann.attrs = *egressAttrs;
            ann.nlri.push_back(nlri);
            update.announcements.push_back(std::move(ann));
            session->sendUpdate<N>(update);
        });
    }

    uint64_t resolveIgpMetric(const IPAddress& nextHop) const
    {
        if (!igpMetricResolver)
            return std::numeric_limits<uint64_t>::max();
        return igpMetricResolver(nextHop);
    }

private:
    AdjRibInTable<NlriT>  adjRibIn;
    LocRibTable<NlriT>    locRib;
    AdjRibOutTable<NlriT> adjRibOut;

    N policy;

    IgpMetricResolver igpMetricResolver;

    BgpProcess& process;
    AfiSafi family;

    Config::Reference<Config::BgpAddressFamilyRegistry> configs;
};
}

#endif // BGP_ADDRESS_FAMILY_H
