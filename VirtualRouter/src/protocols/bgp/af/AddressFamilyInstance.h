// AddressFamilyInstance.h

#ifndef BGP_ADDRESS_FAMILY_INSTANCE_H
#define BGP_ADDRESS_FAMILY_INSTANCE_H

#include <algorithm>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <IPAddress.hpp>
#include <VirtualRouter.h>

#include "Functions.h"
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
    static ProcessQueueRef getScheduler(BgpProcess& proc);
};

inline static AsPathSegment& getAsSegment(Attributes& attrs)
{
    if (!attrs.asPath.empty() && attrs.asPath[0].segmentType == BGP_AS_SEQUENCE)
        return attrs.asPath[0];
    attrs.asPath.insert(attrs.asPath.begin(), AsPathSegment{BGP_AS_SEQUENCE, {}});
    return attrs.asPath.front();
}

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

    // Called on session establishment: advertises all current Loc-RIB routes to the new peer.
    void onPeerEstablished(Session& session)
    {
        const bool enhancedRR = session.getNegotiated().enhancedRR;
        if (enhancedRR)
            session.sendRouteRefresh(family, RouteRefreshReason::Borr);

        for (auto& [nlri, route] : locRib)
            recomputeAdjRibOut(nlri, &route);

        sendDefaultOriginate(session);

        if (enhancedRR)
            session.sendRouteRefresh(family, RouteRefreshReason::Eorr);
    }

    void refreshPeer(Session& session)
    {
        const uint32_t peerRid = session.getPeerRid();
        const bool enhancedRR = session.getNegotiated().enhancedRR;

        if (enhancedRR)
            session.sendRouteRefresh(family, RouteRefreshReason::Borr);

        auto outIt = adjRibOut.find(peerRid);
        if (outIt != adjRibOut.end())
        {
            auto& attrMgr = AddressFamilyInstanceHelper::getAttrMgr(process);

            // Group NLRIs by their stored egress pathId to batch into one Announcement per path.
            std::unordered_map<uint32_t, size_t> pathToAnn;
            BuildUpdate<NlriT> update;

            for (auto& [nlri, pathAndRoute] : outIt->second)
            {
                auto& [addPathId, outRoute] = pathAndRoute;
                if (!outRoute.pathId.has_value())
                    continue;

                PathAttribute pa = attrMgr.get(*outRoute.pathId);
                NlriPath<NlriT> nlriPath{nlri, addPathId};

                auto ait = pathToAnn.find(*outRoute.pathId);
                if (ait == pathToAnn.end())
                {
                    pathToAnn[*outRoute.pathId] = update.announcements.size();
                    typename BuildUpdate<NlriT>::Announcement ann;
                    ann.attrs = std::move(pa);
                    ann.nlri.push_back(nlriPath);
                    update.announcements.push_back(std::move(ann));
                }
                else
                {
                    update.announcements[ait->second].nlri.push_back(nlriPath);
                }
            }

            if (!update.announcements.empty())
                session.sendUpdate<N>(update);
        }

        sendDefaultOriginate(session);

        if (enhancedRR)
            session.sendRouteRefresh(family, RouteRefreshReason::Eorr);
    }

    void sendDefaultOriginate(Session& session)
    {
        if constexpr (!std::is_same_v<NlriT, IPPrefix>)
            return;

        Neighbor& nbr = session.getNeighbor();
        NeighborAf& afNbr = nbr.getAfNeighbor(family);

        if (!afNbr.getConfigs().get<Config::BgpNeighbor::ACTIVATE>().load())
            return;
        if (!afNbr.getConfigs().get<Config::BgpNeighbor::DEFAULT_ORIGINATE>().load())
            return;

        // Build default NLRI: 0.0.0.0/0 or ::/0
        NlriT defaultNlri{};
        if constexpr (N::afi.afi == BGP_AFI_IPV6)
            defaultNlri.af = ::AddressFamily::IPv6;
        else
            defaultNlri.af = ::AddressFamily::IPv4;

        const bool isEbgp = session.isEbgp();
        const uint32_t routerAs = AddressFamilyInstanceHelper::getAsNum(process);
        auto& sesCfgs = nbr.getConfigs();

        PathAttribute pa{};
        pa.attrs.origin = BGP_ORIGIN_IGP;

        if (isEbgp)
        {
            AsPathSegment seg;
            seg.segmentType = BGP_AS_SEQUENCE;

            bool localAsEnabled = sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS>().load();
            auto& localAsField  = sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_AS>();

            if (!localAsEnabled || !localAsField.hasValue())
            {
                seg.asns.push_back(routerAs);
            }
            else if (sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_REPLACE_AS>().load())
            {
                seg.asns.push_back(localAsField.load());
            }
            else if (sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_NO_PREPEND>().load())
            {
                seg.asns.push_back(routerAs);
            }
            else
            {
                seg.asns.push_back(localAsField.load());
                seg.asns.push_back(routerAs);
            }

            pa.attrs.asPath.push_back(std::move(seg));
        }
        else
        {
            pa.attrs.localPref = 100;
        }

        if (auto* conn = session.getPrimaryConnection())
            pa.path.nextHop = conn->socketKey()->local.address;

        BuildUpdate<NlriT> update;
        typename BuildUpdate<NlriT>::Announcement ann;
        ann.attrs = std::move(pa);
        ann.nlri.push_back({defaultNlri, 0});
        update.announcements.push_back(std::move(ann));
        session.sendUpdate<N>(update);
        defaultOriginatedPeers.insert(session.getPeerRid());
    }

    void withdrawDefaultOriginate(Session& session)
    {
        if constexpr (!std::is_same_v<NlriT, IPPrefix>)
            return;

        const uint32_t peerRid = session.getPeerRid();
        if (!defaultOriginatedPeers.erase(peerRid))
            return;

        NlriT defaultNlri{};
        if constexpr (N::afi.afi == BGP_AFI_IPV6)
            defaultNlri.af = ::AddressFamily::IPv6;
        else
            defaultNlri.af = ::AddressFamily::IPv4;

        BuildUpdate<NlriT> withdraw;
        withdraw.withdrawn.push_back({defaultNlri, 0});
        session.sendUpdate<N>(withdraw);
    }

    void invalidatePeer(uint32_t peer)
    {
        auto mraiIt = mraiState.find(peer);
        if (mraiIt != mraiState.end())
        {
            if (mraiIt->second.timerId != 0)
                AddressFamilyInstanceHelper::getScheduler(process).cancel(mraiIt->second.timerId);
            mraiState.erase(mraiIt);
        }

        if (Neighbor* nbr = AddressFamilyInstanceHelper::getNtable(process).lookup(peer))
            nbr->getAfNeighbor(family).orfFilter.clear();

        defaultOriginatedPeers.erase(peer);

        auto outIt = adjRibOut.find(peer);
        if (outIt != adjRibOut.end())
            adjRibOut.erase(outIt);

        auto it = adjRibIn.find(peer);
        if (it == adjRibIn.end())
            return;

        std::unordered_set<NlriT> keys;
        keys.reserve(it->second.size());
        for (const auto& [nlriPath, inRoute] : it->second)
        {
            keys.insert(nlriPath.nlri);
            auto lit = locRib.find(nlriPath.nlri);
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

        std::unordered_set<NlriT> touched;

        // Withdrawn: n is NlriPath<NlriT>
        for (const auto& n : update.withdrawn)
        {
            auto it = peerIn.find(n);
            if (it != peerIn.end())
            {
                auto lit = locRib.find(n.nlri);
                if (lit != locRib.end() && &lit->second.in == &it->second)
                    locRib.erase(lit);
                peerIn.erase(it);
            }
            touched.insert(n.nlri);
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

                // n is NlriPath<NlriT>; pass n.nlri to InboundRoute constructor
                InboundRoute<NlriT> r(attrMgr, pid, n.nlri, &naf);
                r.neighborRouterId = peer.rid;
                r.peerAs            = peerAs;
                r.ebgp              = isEbgp;
                r.igpCost           = resolveIgpMetric(update.attrs->path.nextHop);

                auto& weight = nbr->getAfNeighbor(family).getConfigs().get<Config::BgpNeighbor::WEIGHT>();
                if (weight.hasValue()) r.weigth = weight.load();

                if (applyIngressPolicy(r))
                    continue;

                // Erase old entry for this (nlri, pathId); clear locRib pointer if needed.
                {
                    auto existing = peerIn.find(n);
                    if (existing != peerIn.end())
                    {
                        auto lit = locRib.find(n.nlri);
                        if (lit != locRib.end() && &lit->second.in == &existing->second)
                            locRib.erase(lit);
                        peerIn.erase(existing);
                    }
                }
                peerIn.emplace(n, std::move(r));
                touched.insert(n.nlri);
            }
        }

        // MAXIMUM_PREFIX enforcement
        {
            auto& nbrAfCfgs = nbr->getAfNeighbor(family).getConfigs();
            auto& maxPfxField = nbrAfCfgs.get<Config::BgpNeighbor::MAXIMUM_PREFIX>();
            if (maxPfxField.hasValue())
            {
                uint32_t maxPfx = maxPfxField.load();
                uint32_t count = static_cast<uint32_t>(peerIn.size());
                bool warningOnly = nbrAfCfgs.get<Config::BgpNeighbor::MAXIMUM_PREFIX_WARNING_ONLY>().load();

                if (count >= maxPfx && !warningOnly && peer.session)
                    peer.session->postEvent(FsmEvent::MAX_PREFIX_REACHED);
            }
        }

        for (const auto& n : touched)
            recomputeNlri(n);
    }

    void recomputeNlri(const NlriT& nlri)
    {
        std::vector<InboundRoute<NlriT>*> candidates;

        for (auto& [peer, peerTable] : adjRibIn)
        {
            for (auto& [key, route] : peerTable)
            {
                if (key.nlri != nlri)
                    continue;
                // Refresh IGP cost before running best-path.
                auto attrs = route.getPathAttributes();
                if (attrs.has_value())
                    route.igpCost = resolveIgpMetric(attrs->path.nextHop);
                candidates.push_back(&route);
            }
        }

        BestPathConfig bpCfg;
        bpCfg.compareRouterId   = configs->get<Config::BgpAddressFamily::BGP_BEST_PATH_COMPARE_ROUTER_ID>().load();
        bpCfg.medMissingAsWorst = configs->get<Config::BgpAddressFamily::BGP_BEST_PATH_MED_MISSING_AS_WORST>().load();
        bpCfg.ignoreIgpMetric   = configs->get<Config::BgpAddressFamily::BGP_BEST_PATH_IGP_METRIC_IGNORE>().load();

        DecisionEngine decision(process, bpCfg);
        std::optional<LocalRoute<NlriT>> best = decision.selectBest(
            candidates,
            configs->get<Config::BgpAddressFamily::MAXIMUM_PATHS_EBGP>().load(),
            configs->get<Config::BgpAddressFamily::MAXIMUM_PATHS_IBGP>().load()
        );

        // Build ADD-PATH candidate pool for additional-paths advertisement.
        if (best.has_value())
        {
            bool selectAll       = configs->get<Config::BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_ALL>().load();
            bool selectBackup    = configs->get<Config::BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BACKUP>().load();
            auto& selectBestFld  = configs->get<Config::BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BEST>();
            bool selectBestExt   = configs->get<Config::BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL>().load();
            bool selectGroupBest = configs->get<Config::BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_GROUP_BEST>().load();

            if (selectAll || selectBackup || selectBestFld.hasValue() || selectBestExt || selectGroupBest)
            {
                std::vector<InboundRoute<NlriT>*> pool;
                for (auto* r : decision.rankCandidates(candidates))
                {
                    if (r == &best->in) continue;
                    if (std::find(best->multipaths.begin(), best->multipaths.end(), r) != best->multipaths.end()) continue;
                    pool.push_back(r);
                }

                if (selectAll)
                {
                    best->additionalPaths = pool;
                }
                else
                {
                    auto tryAdd = [&](InboundRoute<NlriT>* r) {
                        if (std::find(best->additionalPaths.begin(), best->additionalPaths.end(), r) == best->additionalPaths.end())
                            best->additionalPaths.push_back(r);
                    };

                    if (selectBestFld.hasValue())
                    {
                        uint8_t n = selectBestFld.load();
                        for (auto* r : pool) {
                            if (best->additionalPaths.size() >= n) break;
                            tryAdd(r);
                        }
                    }

                    if (selectBackup && !pool.empty())
                        tryAdd(pool[0]);

                    if (selectBestExt)
                        for (auto* r : pool)
                            if (r->ebgp) { tryAdd(r); break; }

                    if (selectGroupBest)
                    {
                        std::unordered_set<uint32_t> seenAs;
                        seenAs.insert(best->in.peerAs);
                        for (auto* mp : best->multipaths)
                            seenAs.insert(mp->peerAs);
                        for (auto* r : pool)
                            if (seenAs.insert(r->peerAs).second)
                                tryAdd(r);
                    }
                }
            }
        }

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

        const bool bestChanged = !had || &lit->second.in != &best->in;

        // Always refresh the locRib entry so multipaths/additionalPaths stay current.
        if (had)
            locRib.erase(lit);
        locRib.emplace(nlri, *best);

        if (bestChanged)
            installToRib(locRib.at(nlri));

        recomputeAdjRibOut(nlri, &locRib.at(nlri));
    }

    void installToRib(LocalRoute<NlriT>& route)
    {
        policy.installRoute(route);
    }

    void withdrawFromRib(const NlriT& nlri)
    {
        policy.withdrawRoute(nlri);
    }

    // Returns true if the route should be DROPPED (filtered out); false to accept into Adj-RIB-In.
    bool applyIngressPolicy(const InboundRoute<NlriT>& route)
    {
        PathAttribute pathAttrs = AddressFamilyInstanceHelper::getAttrMgr(process).get(*route.pathId);
        uint32_t routerAs = AddressFamilyInstanceHelper::getAsNum(process);
        auto& nbr = route.sourceNeighbor->globalNbr();

        // AS-PATH loop prevention (check all segments)
        {
            NeighborConfigs& nbrCfgs = nbr.getConfigs();
            bool localAsEnabled = nbrCfgs.get<Config::BgpNeighborSession::LOCAL_AS>().load();
            bool dualAs = nbrCfgs.get<Config::BgpNeighborSession::LOCAL_AS_DUAL_AS>().load();
            auto& localAsField = nbrCfgs.get<Config::BgpNeighborSession::LOCAL_AS_AS>();

            NeighborAfConfigs& nbrAfCfgs = nbr.getAfNeighbor(family).getConfigs();
            bool allowAsIn = nbrAfCfgs.get<Config::BgpNeighbor::ALLOWAS_IN>().load();
            uint8_t maxOccurrences = 1;
            if (allowAsIn)
            {
                auto& occField = nbrAfCfgs.get<Config::BgpNeighbor::ALLOWAS_IN_OCCURANCES>();
                if (occField.hasValue())
                    maxOccurrences = occField.load();
            }

            uint32_t ownAsCount = 0;
            uint32_t localAsCount = 0;
            for (const auto& seg : pathAttrs.attrs.asPath)
            {
                for (uint32_t asn : seg.asns)
                {
                    if (asn == routerAs)
                        ownAsCount++;
                    if (localAsEnabled && !dualAs && localAsField.hasValue() && asn == localAsField.load())
                        localAsCount++;
                }
            }

            if (!allowAsIn && ownAsCount > 0)
                return true; // reject: AS-PATH loop
            if (allowAsIn && ownAsCount > maxOccurrences)
                return true; // reject: exceeds allowas-in limit
            if (localAsCount > 0)
                return true; // reject: local-as loop
        }

        // TODO: ingress policy (route-maps, prefix-lists, community filters, etc.)
        return false; // accept
    }

    // Group-level egress: strip LOCAL_PREF and prepend AS-path.
    // Result is shared by all members of the same peer group + isEbgp combination.
    std::optional<PathAttribute> applyGroupEgressPolicy(const InboundRoute<NlriT>& route, const Session& session)
    {
        auto pa_opt = route.getPathAttributes();
        if (!pa_opt.has_value())
            return std::nullopt;

        PathAttribute pa = *pa_opt;

        if (session.isEbgp())
        {
            auto& sesCfgs = session.getNeighbor().getConfigs();

            pa.attrs.localPref = std::nullopt;

            AsPathSegment& seg = getAsSegment(pa.attrs);
            uint32_t routerAs = AddressFamilyInstanceHelper::getAsNum(process);
            auto& localAs = sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_AS>();
            if (!sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS>().load() || !localAs.hasValue())
                seg.asns.insert(seg.asns.begin(), routerAs);
            else if (sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_REPLACE_AS>().load())
                seg.asns.insert(seg.asns.begin(), localAs.load());
            else if (sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_NO_PREPEND>().load())
                seg.asns.insert(seg.asns.begin(), routerAs);
            else
            {
                seg.asns.insert(seg.asns.begin(), routerAs);
                seg.asns.insert(seg.asns.begin(), localAs.load());
            }
        }

        return pa;
    }

    // Per-member nexthop adjustment, applied after group-level policy.
    void applyMemberNexthop(PathAttribute& pa, const InboundRoute<NlriT>& route,
                            const NeighborAf& afNbr, const Session& session)
    {
        const auto& cfgs = afNbr.getConfigs();

        if (session.isEbgp())
        {
            if (!cfgs.get<Config::BgpNeighbor::NEXT_HOP_UNCHANGED>().load() ||
                cfgs.get<Config::BgpNeighbor::NEXT_HOP_SELF_ALL>().load())
                pa.path.nextHop = session.getNeighbor().neighborAddress;

            // SEND_COMMUNITY: strip communities for eBGP unless explicitly enabled.
            bool sendStd = cfgs.get<Config::BgpNeighbor::SEND_COMMUNITY>().load()
                        || cfgs.get<Config::BgpNeighbor::SEND_COMMUNITY_BOTH>().load()
                        || cfgs.get<Config::BgpNeighbor::SEND_COMMUNITY_STANDARD>().load();
            bool sendExt = cfgs.get<Config::BgpNeighbor::SEND_COMMUNITY_EXTENDED>().load()
                        || cfgs.get<Config::BgpNeighbor::SEND_COMMUNITY_BOTH>().load();

            if (!sendStd)
                pa.attrs.communities.clear();
            if (!sendExt)
                pa.attrs.extendedCommunities.clear();
            if (!sendStd && !sendExt)
                pa.attrs.largeCommunities.clear();

            // REMOVE_PRIVATE_AS: strip private ASNs from egress AS-PATH.
            bool removePrivate = cfgs.get<Config::BgpNeighbor::REMOVE_PRIVATE_AS>().load();
            bool removeAll     = cfgs.get<Config::BgpNeighbor::REMOVE_PRIVATE_AS_ALL>().load();
            if (removePrivate || removeAll)
            {
                auto isPrivateAs = [](uint32_t asn) {
                    return (asn >= 64512u && asn <= 65534u) ||
                           (asn >= 4200000000u && asn <= 4294967294u);
                };
                for (auto& seg : pa.attrs.asPath)
                {
                    auto it = std::remove_if(seg.asns.begin(), seg.asns.end(), isPrivateAs);
                    seg.asns.erase(it, seg.asns.end());
                }
                auto it = std::remove_if(pa.attrs.asPath.begin(), pa.attrs.asPath.end(),
                    [](const AsPathSegment& s) { return s.asns.empty(); });
                pa.attrs.asPath.erase(it, pa.attrs.asPath.end());
            }
        }
        else
        {
            if ((cfgs.get<Config::BgpNeighbor::NEXT_HOP_SELF>().load() &&
                 route.neighborRouterId != AddressFamilyInstanceHelper::getRid(process)) ||
                cfgs.get<Config::BgpNeighbor::NEXT_HOP_SELF_ALL>().load())
                pa.path.nextHop = session.getPrimaryConnection()->socketKey()->local.address;
        }
    }

    // Combined egress policy for ungrouped neighbors.
    std::optional<PathAttribute> applyEgressPolicy(const InboundRoute<NlriT>& route, const Session& session)
    {
        auto pa = applyGroupEgressPolicy(route, session);
        if (!pa.has_value())
            return std::nullopt;

        applyMemberNexthop(*pa, route, session.getNeighbor().getAfNeighbor(family), session);
        return pa;
    }

    // Called by the MRAI timer: bypasses the rate-limit check and recomputes for all deferred NLRIs.
    void drainMraiPending(uint32_t peerRid)
    {
        auto it = mraiState.find(peerRid);
        if (it == mraiState.end()) return;

        std::unordered_set<NlriT> pending = std::move(it->second.pending);
        it->second.timerId = 0;
        it->second.lastSent = std::chrono::steady_clock::now();

        mraiBypassPeer = peerRid;
        for (const NlriT& nlri : pending)
        {
            auto lit = locRib.find(nlri);
            recomputeAdjRibOut(nlri, lit != locRib.end() ? &lit->second : nullptr);
        }
        mraiBypassPeer = 0;
    }

    void recomputeAdjRibOut(const NlriT& nlri, LocalRoute<NlriT>* best)
    {
        auto& attrMgr = AddressFamilyInstanceHelper::getAttrMgr(process);

        AddressFamilyInstanceHelper::getNtable(process).forEachNeighbor([&](Neighbor& nbr) {
            Session* session = nbr.session;
            if (!session || !session->established())
                return;

            const uint32_t peerRid = session->getPeerRid();
            PerPeerOutTable<NlriT>& peerOut = adjRibOut[peerRid];

            auto withdrawFromPeer = [&]() {
                auto [begin, end] = peerOut.equal_range(nlri);
                if (begin == end) return;
                BuildUpdate<NlriT> withdraw;
                for (auto it = begin; it != end; ++it)
                    withdraw.withdrawn.push_back({nlri, it->second.first});
                peerOut.erase(begin, end);
                session->sendUpdate<N>(withdraw);
            };

            if (!best)
            {
                withdrawFromPeer();
                return;
            }

            // MRAI: rate-limit announcements. Withdrawals always bypass.
            if (peerRid != mraiBypassPeer)
            {
                uint16_t mraiSecs = nbr.getAfNeighbor(family).getConfigs()
                    .get<Config::BgpNeighbor::ADVERTISE_INTERVAL>().load();
                if (mraiSecs > 0)
                {
                    auto& ms = mraiState[peerRid];
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - ms.lastSent);
                    if (elapsed.count() < mraiSecs)
                    {
                        ms.pending.insert(nlri);
                        if (ms.timerId == 0)
                        {
                            auto expiry = ms.lastSent + std::chrono::seconds(mraiSecs);
                            ms.timerId = AddressFamilyInstanceHelper::getScheduler(process).postAfter(expiry,
                                [this, peerRid](uint32_t) { drainMraiPending(peerRid); });
                        }
                        return;
                    }
                }
            }

            // Check that this AFI/SAFI was negotiated with this peer.
            const auto& negotiated = session->getNegotiated();
            if (!negotiated.activeFamilies.count(family))
            {
                withdrawFromPeer();
                return;
            }

            // iBGP split-horizon with route-reflector awareness.
            const bool fromIbgp = !best->in.ebgp;
            const bool toIbgp   = !session->isEbgp();
            if (fromIbgp && toIbgp)
            {
                const bool targetIsClient = nbr.getAfNeighbor(family).getConfigs()
                    .get<Config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>().load();
                const bool senderIsClient = best->in.sourceNeighbor->getConfigs()
                    .template get<Config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>().load();

                if (!senderIsClient && !targetIsClient)
                {
                    withdrawFromPeer();
                    return;
                }
                if (&best->in.sourceNeighbor->globalNbr() == &nbr)
                {
                    withdrawFromPeer();
                    return;
                }
            }

            NeighborAf& afNbr = nbr.getAfNeighbor(family);

            // ACTIVATE: only exchange routes when this AF is explicitly activated for the neighbor.
            if (!afNbr.getConfigs().get<Config::BgpNeighbor::ACTIVATE>().load())
            {
                withdrawFromPeer();
                return;
            }

            // ORF: apply peer-specified prefix-list filter on our outbound.
            if (!afNbr.orfFilter.empty() && !passesOrfFilter(nlri, afNbr.orfFilter))
            {
                withdrawFromPeer();
                return;
            }

            PeerGroup* pg = afNbr.getConfigs().getPeerGroup();
            const bool addPathSend = negotiated.addPathSend(family);
            auto& nbrAfCfgs = afNbr.getConfigs();

            // Collect all paths to advertise based on per-neighbor ADVERTISE configs.
            std::vector<InboundRoute<NlriT>*> paths;
            paths.push_back(&best->in);

            if (addPathSend)
            {
                if (nbrAfCfgs.get<Config::BgpNeighbor::ADVERTISE_DIVERSE_PATH_MPATH>().load())
                    for (auto* mp : best->multipaths)
                        paths.push_back(mp);

                if (!best->additionalPaths.empty())
                {
                    bool advAll       = nbrAfCfgs.get<Config::BgpNeighbor::ADVERTISE_ADDITIONAL_PATHS_ALL>().load();
                    auto& advBestFld  = nbrAfCfgs.get<Config::BgpNeighbor::ADVERTISE_ADDITIONAL_PATHS_BEST>();
                    bool advGroupBest = nbrAfCfgs.get<Config::BgpNeighbor::ADVERTISE_ADDITIONAL_GROUP_BEST>().load();
                    bool advBestExt   = nbrAfCfgs.get<Config::BgpNeighbor::ADVERTISE_BEST_EXTERNAL>().load();
                    bool advBackup    = nbrAfCfgs.get<Config::BgpNeighbor::ADVERTISE_DIVERSE_PATH_BACKUP>().load();

                    if (advAll)
                    {
                        for (auto* r : best->additionalPaths)
                            paths.push_back(r);
                    }
                    else if (advBestFld.hasValue() || advGroupBest || advBestExt || advBackup)
                    {
                        const size_t base = paths.size();
                        auto tryAdd = [&](InboundRoute<NlriT>* r) {
                            if (std::find(paths.begin() + base, paths.end(), r) == paths.end())
                                paths.push_back(r);
                        };

                        if (advBestFld.hasValue())
                        {
                            uint8_t n = advBestFld.load();
                            for (auto* r : best->additionalPaths) {
                                if (paths.size() - base >= n) break;
                                tryAdd(r);
                            }
                        }

                        if (advBackup && !best->additionalPaths.empty())
                            tryAdd(best->additionalPaths[0]);

                        if (advBestExt)
                            for (auto* r : best->additionalPaths)
                                if (r->ebgp) { tryAdd(r); break; }

                        if (advGroupBest)
                        {
                            std::unordered_set<uint32_t> seenAs;
                            for (auto* r : paths)
                                seenAs.insert(r->peerAs);
                            for (auto* r : best->additionalPaths)
                                if (seenAs.insert(r->peerAs).second)
                                    tryAdd(r);
                        }
                    }
                }
            }

            BuildUpdate<NlriT> update;

            // For ADD-PATH: remove out entries whose source path is no longer active.
            if (addPathSend)
            {
                auto [begin, end] = peerOut.equal_range(nlri);
                for (auto it = begin; it != end; )
                {
                    uint32_t apid = it->second.first;
                    bool stillActive = false;
                    for (auto* r : paths)
                    {
                        if (r->sourceNeighbor && r->sourceNeighbor->globalNbr().rid == apid)
                        { stillActive = true; break; }
                    }
                    if (!stillActive)
                    {
                        update.withdrawn.push_back({nlri, apid});
                        it = peerOut.erase(it);
                    }
                    else
                        ++it;
                }
            }

            for (auto* route : paths)
            {
                // For ADD-PATH peers, use source peer RID as path ID; otherwise 0.
                uint32_t egressPathId = (addPathSend && route->sourceNeighbor)
                    ? route->sourceNeighbor->globalNbr().rid : 0;

                std::optional<PathAttribute> egressAttrs;
                if (pg)
                {
                    auto groupAttrs = applyGroupEgressPolicy(*route, *session);
                    if (!groupAttrs.has_value())
                        continue;
                    applyMemberNexthop(*groupAttrs, *route, afNbr, *session);
                    egressAttrs = std::move(groupAttrs);
                }
                else
                {
                    egressAttrs = applyEgressPolicy(*route, *session);
                    if (!egressAttrs.has_value())
                        continue;
                }

                uint32_t oPid = attrMgr.acquire(egressAttrs->attrs, egressAttrs->path);

                // Find any existing out entry for this (nlri, egressPathId).
                auto [begin, end] = peerOut.equal_range(nlri);
                auto existing = std::find_if(begin, end,
                    [&](const auto& e) { return e.second.first == egressPathId; });
                if (existing != end && existing->second.second.pathId == oPid)
                {
                    attrMgr.release(oPid);
                    continue;
                }
                if (existing != end)
                    peerOut.erase(existing);

                peerOut.emplace(nlri, std::make_pair(egressPathId, OutboundRoute<NlriT>{attrMgr, oPid, nlri}));

                typename BuildUpdate<NlriT>::Announcement ann;
                ann.attrs = *egressAttrs;
                ann.nlri.push_back({nlri, egressPathId});
                update.announcements.push_back(std::move(ann));
            }

            if (!update.announcements.empty() || !update.withdrawn.empty())
                session->sendUpdate<N>(update);

            if (!update.announcements.empty())
            {
                mraiState[peerRid].lastSent = std::chrono::steady_clock::now();
                mraiState[peerRid].pending.erase(nlri);
            }
        });
    }

    uint64_t resolveIgpMetric(const IPAddress& nextHop) const
    {
        if (!igpMetricResolver)
            return std::numeric_limits<uint64_t>::max();
        return igpMetricResolver(nextHop);
    }

    bool passesOrfFilter(const NlriT& nlri, const std::vector<OrfPrefixEntry>& filter) const
    {
        if constexpr (std::is_same_v<NlriT, IPPrefix>)
        {
            for (const auto& e : filter)
            {
                if (e.action != BGP_ORF_ACTION_ADD) continue;

                uint8_t minLen = e.minLen;
                uint8_t maxLen = e.maxLen;
                if (minLen == 0 && maxLen == 0)
                    minLen = maxLen = e.prefix.prefixLength;

                if (nlri.prefixLength < minLen || nlri.prefixLength > maxLen)
                    continue;

                // Check if nlri is a subnet of e.prefix
                if (!Functions::compareNetworkWithIp(e.prefix.addr, nlri.addr, e.prefix.prefixLength, e.prefix.af))
                    continue;

                return e.match == BGP_ORF_MATCH_PERMIT;
            }
            return false; // implicit deny when filter is non-empty and nothing matched
        }
        else
        {
            return true; // ORF prefix-list not applicable for this NLRI type
        }
    }

private:
    struct MraiState
    {
        std::chrono::steady_clock::time_point lastSent{};
        std::unordered_set<NlriT> pending;
        uint32_t timerId = 0;
    };

    AdjRibInTable<NlriT>  adjRibIn;
    LocRibTable<NlriT>    locRib;
    AdjRibOutTable<NlriT> adjRibOut;
    std::unordered_map<uint32_t, MraiState> mraiState;
    uint32_t mraiBypassPeer = 0;
    std::unordered_set<uint32_t> defaultOriginatedPeers; // RIDs that have received the synthetic default

    N policy;

    IgpMetricResolver igpMetricResolver;

    BgpProcess& process;
    AfiSafi family;

    Config::Reference<Config::BgpAddressFamilyRegistry> configs;
};
}

#endif // BGP_ADDRESS_FAMILY_H
