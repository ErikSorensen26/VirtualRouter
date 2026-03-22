// AddressFamilyInstance.h

#ifndef BGP_ADDRESS_FAMILY_INSTANCE_H
#define BGP_ADDRESS_FAMILY_INSTANCE_H

#include <algorithm>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <IPAddress.h>
#include <VirtualRouter.h>

#include "ProcessAccessor.h"
#include "bgp/BgpTypes.hpp"
#include "bgp/session/Session.h"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/transport/BgpRx.h"
#include "bgp/transport/BgpTx.h"
#include "bgp/decision/DecisionEngine.hpp"
#include "bgp/neighbor/NeighborTable.h"
#include "bgp/neighbor/NeighborAf.h"
#include "bgp/attributes/AttributeManager.hpp"
#include "bgp/features/Dampening.h"

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
inline static AsPathSegment& getAsSegment(Attributes& attrs)
{
    if (!attrs.asPath.empty() && attrs.asPath[0].segmentType == BGP_AS_SEQUENCE)
        return attrs.asPath[0];
    attrs.asPath.insert(attrs.asPath.begin(), AsPathSegment{BGP_AS_SEQUENCE, {}});
    return attrs.asPath.front();
}

inline static AsPathSegment& getConfedAsSegment(Attributes& attrs)
{
    if (!attrs.asPath.empty() && attrs.asPath[0].segmentType == BGP_AS_CONFED_SEQUENCE)
        return attrs.asPath[0];
    attrs.asPath.insert(attrs.asPath.begin(), AsPathSegment{BGP_AS_CONFED_SEQUENCE, {}});
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
          policy(ProcessAccessor::getRoutingInstance(proc)),
          igpMetricResolver([](const IPAddress&) { return std::numeric_limits<uint64_t>::max(); }),
          configs([&proc, &fam]() {
              auto& bgpConfigs = ProcessAccessor::getConfigs(proc);
              auto& vrf = ProcessAccessor::getRoutingInstance(proc);
              auto& registry = vrf.getRegistry();
              return registry.emplaceBack(
                  bgpConfigs.get<Config::Bgp::ADDRESS_FAMILIES>(), fam.flatten()
              );
          })
    {
        ProcessAccessor::emplaceAfBase(configs->get<Config::BgpAddressFamily::AF_BASE>(), process);
        syncNetworkRoutes();
    }

    AddressFamilyInstance(const AddressFamilyInstance&) = delete;
    AddressFamilyInstance& operator=(const AddressFamilyInstance&) = delete;
    AddressFamilyInstance(AddressFamilyInstance&&) = delete;
    AddressFamilyInstance& operator=(AddressFamilyInstance&&) = delete;

    ~AddressFamilyInstance()
    {
        auto sched = ProcessAccessor::getScheduler(process);
        if (nhtTimerId != 0)
            sched.cancel(nhtTimerId);
        // Cancel all stale-path timers.
        for (auto& [rid, ids] : staleTimers)
        {
            if (ids.stalepath) sched.cancel(ids.stalepath);
            if (ids.maxEor)    sched.cancel(ids.maxEor);
        }
        clearNetworkWatches();
        clearNhtWatches();
    }

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
        sendActiveAggregatesToPeer(session);

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
            auto& attrMgr = ProcessAccessor::getAttrMgr(process);

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
        sendActiveAggregatesToPeer(session);

        if (enhancedRR)
            session.sendRouteRefresh(family, RouteRefreshReason::Eorr);
    }

    // Called when peer sends BORR (Enhanced Route Refresh begin): mark Adj-RIB-In stale
    // and start STALEPATH / MAX-EOR timers (RFC 7313).
    void onPeerBorr(Session& session)
    {
        const uint32_t peerRid = session.getPeerRid();

        // Cancel any in-flight stale timers from a previous BORR cycle.
        cancelStaleTimers(peerRid);

        // Mark all current inbound routes from this peer as stale.
        auto inIt = adjRibIn.find(peerRid);
        if (inIt != adjRibIn.end())
        {
            auto& stale = stalePeerNlris[peerRid];
            for (const auto& [nlriPath, route] : inIt->second)
                stale.insert(nlriPath.nlri);
        }

        auto sched = ProcessAccessor::getScheduler(process);
        auto& procCfgs = ProcessAccessor::getConfigs(process);

        auto& sti = staleTimers[peerRid];

        uint16_t staleSecs = procCfgs.get<Config::Bgp::BGP_REFRESH_STALEPATH_TIME>().load();
        sti.stalepath = sched.postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(staleSecs),
            [this, peerRid](uint32_t) { staleTimers[peerRid].stalepath = 0; purgeStalePeer(peerRid); });

        uint16_t maxEorSecs = procCfgs.get<Config::Bgp::BGP_REFRESH_MAX_EOR_TIME>().load();
        sti.maxEor = sched.postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(maxEorSecs),
            [this, peerRid](uint32_t) { staleTimers[peerRid].maxEor = 0; purgeStalePeer(peerRid); });
    }

    // Called when peer sends EORR (Enhanced Route Refresh end): purge stale paths.
    void onPeerEorr(Session& session)
    {
        const uint32_t peerRid = session.getPeerRid();
        cancelStaleTimers(peerRid);
        purgeStalePeer(peerRid);
    }

    // Called by BGP_SCAN_TIME periodic timer to re-evaluate all active routes.
    void scan()
    {
        for (auto& [nlri, route] : locRib)
            recomputeAdjRibOut(nlri, &route);
    }

    void softClearInbound(uint32_t peerRid)
    {
        auto preIt = preAdjRibIn.find(peerRid);
        if (preIt == preAdjRibIn.end()) return;

        Neighbor* nbr = ProcessAccessor::getNtable(process).lookup(peerRid);
        if (!nbr) return;

        auto& attrMgr = ProcessAccessor::getAttrMgr(process);
        NeighborAf& nbrAf = nbr->getAfNeighbor(family);

        // Remove existing post-policy routes for this peer from locRib + adjRibIn.
        std::unordered_set<NlriT> touched;
        auto inIt = adjRibIn.find(peerRid);
        if (inIt != adjRibIn.end())
        {
            for (const auto& [nlriPath, inRoute] : inIt->second)
            {
                auto lit = locRib.find(nlriPath.nlri);
                if (lit != locRib.end() && &lit->second.route == &inRoute)
                    locRib.erase(lit);
                touched.insert(nlriPath.nlri);
            }
            inIt->second.clear();
        }

        // Re-apply ingress policy to each stored pre-policy route.
        auto& peerIn = adjRibIn[peerRid];
        for (auto& [nlriPath, entry] : preIt->second)
        {
            uint32_t pid = attrMgr.acquire(entry.pa.attrs, entry.pa.path);
            InboundRoute<NlriT> r(attrMgr, pid, nlriPath.nlri, &nbrAf);
            r.neighborRouterId = peerRid;
            r.peerAs            = entry.peerAs;
            r.ebgp              = entry.ebgp;
            r.confedEbgp        = entry.confedEbgp;
            r.igpCost           = resolveIgpMetric(entry.pa.path.nextHop);

            if (applyIngressPolicy(r))
                continue;

            auto existing = peerIn.find(nlriPath);
            if (existing != peerIn.end())
            {
                auto lit = locRib.find(nlriPath.nlri);
                if (lit != locRib.end() && &lit->second.route == &existing->second)
                    locRib.erase(lit);
                peerIn.erase(existing);
            }
            peerIn.emplace(nlriPath, std::move(r));
            touched.insert(nlriPath.nlri);
        }

        for (const NlriT& nlri : touched)
            recomputeNlri(nlri);
    }

    void invalidatePeer(uint32_t peer)
    {
        auto mraiIt = mraiState.find(peer);
        if (mraiIt != mraiState.end())
        {
            if (mraiIt->second.timerId != 0)
                ProcessAccessor::getScheduler(process).cancel(mraiIt->second.timerId);
            mraiState.erase(mraiIt);
        }

        if (Neighbor* nbr = ProcessAccessor::getNtable(process).lookup(peer))
        {
            NeighborAf& nbrAf = nbr->getAfNeighbor(family);
            nbrAf.orfFilter.clear();
            nbrAf.maxPfxWarned = false;
            nbrAf.cancelPfxRestart();
            nbrAf.isSlowPeer = false;
            nbrAf.slowFirstSeen = {};
        }

        cancelStaleTimers(peer);
        preAdjRibIn.erase(peer);
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
            if (lit != locRib.end() && &lit->second.route == &inRoute)
                locRib.erase(lit);
        }

        adjRibIn.erase(it);

        for (const auto& n : keys)
            recomputeNlri(n);
    }

private:
    void onParsedUpdateFromPeer(Neighbor& peer, ParsedUpdate<NlriT>& update)
    {
        Neighbor* nbr = ProcessAccessor::getNtable(process).lookup(peer.rid);
        if (!nbr) return;

        PerPeerInTable<NlriT>& peerIn = adjRibIn[peer.rid];

        NeighborAf& nbrAf = nbr->getAfNeighbor(family);
        auto& nbrAfCfgs = nbrAf.getConfigs();
        bool softReconfig = nbrAfCfgs.get<Config::BgpNeighbor::SOFT_RECONFIGURATION>().load()
                         || configs->get<Config::BgpAddressFamily::BGP_SOFT_RECONFIG_BACKUP>().load();

        std::unordered_set<NlriT> touched;

        // Withdrawn: n is NlriPath<NlriT>
        for (const auto& n : update.withdrawn)
        {
            auto it = peerIn.find(n);
            if (it != peerIn.end())
            {
                auto lit = locRib.find(n.nlri);
                if (lit != locRib.end() && &lit->second.route == &it->second)
                    locRib.erase(lit);
                peerIn.erase(it);
            }
            if (softReconfig)
            {
                auto preIt = preAdjRibIn.find(peer.rid);
                if (preIt != preAdjRibIn.end())
                    preIt->second.erase(n);
            }
            // Peer withdrew this NLRI explicitly; remove from stale set.
            {
                auto stIt = stalePeerNlris.find(peer.rid);
                if (stIt != stalePeerNlris.end())
                    stIt->second.erase(n.nlri);
            }
            touched.insert(n.nlri);
        }

        if (update.attrs.has_value())
        {
            auto& attrMgr = ProcessAccessor::getAttrMgr(process);

            auto& remAs = peer.getConfigs().get<Config::BgpNeighborSession::REMOTE_AS>();
            const uint32_t peerAs = remAs.hasValue() ? remAs.load() : 0;
            const bool isEbgp     = nbr->isEbgp();
            const bool isConfed   = nbr->isConfedEbgp();

            uint32_t pid = attrMgr.acquire(update.attrs->attrs, update.attrs->path);

            // Store pre-policy copy of all announced NLRIs for soft-reconfiguration.
            if (softReconfig)
            {
                auto& preIn = preAdjRibIn[peer.rid];
                for (const auto& n : update.announcements)
                    preIn.insert_or_assign(n, SoftPreEntry{*update.attrs, peerAs, isEbgp, isConfed});
            }

            bool first = true;
            for (const auto& n : update.announcements)
            {
                if (!first)
                    attrMgr.retain(pid);
                first = false;

                // n is NlriPath<NlriT>; pass n.nlri to InboundRoute constructor
                InboundRoute<NlriT> r(attrMgr, pid, n.nlri, &nbrAf);
                r.neighborRouterId = peer.rid;
                r.peerAs           = peerAs;
                r.ebgp             = isEbgp;
                r.confedEbgp       = isConfed;
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
                        if (lit != locRib.end() && &lit->second.route == &existing->second)
                            locRib.erase(lit);
                        peerIn.erase(existing);
                    }
                }
                peerIn.emplace(n, std::move(r));
                // Peer re-advertised this NLRI; it is no longer stale.
                {
                    auto stIt = stalePeerNlris.find(peer.rid);
                    if (stIt != stalePeerNlris.end())
                        stIt->second.erase(n.nlri);
                }
                touched.insert(n.nlri);
            }
        }

        // MAXIMUM_PREFIX enforcement
        {
            auto& maxPfxField = nbrAfCfgs.get<Config::BgpNeighbor::MAXIMUM_PREFIX>();
            if (maxPfxField.hasValue())
            {
                uint32_t maxPfx = maxPfxField.load();
                uint32_t count = static_cast<uint32_t>(peerIn.size());
                bool warningOnly = nbrAfCfgs.get<Config::BgpNeighbor::MAXIMUM_PREFIX_WARNING_ONLY>().load();

                // Threshold warning: fire once per session when count reaches N% of limit.
                auto& threshField = nbrAfCfgs.get<Config::BgpNeighbor::MAXIMUM_PREFIX_THRESHOLD>();
                uint8_t threshold = threshField.hasValue() ? threshField.load() : 75;
                if (!nbrAf.maxPfxWarned && count >= maxPfx * threshold / 100)
                {
                    nbrAf.maxPfxWarned = true;
                    // TODO: log warning
                }

                if (count >= maxPfx && !warningOnly && peer.session)
                {
                    auto& restartField = nbrAfCfgs.get<Config::BgpNeighbor::MAXIMUM_PREFIX_RESTART>();
                    if (restartField.hasValue())
                        nbrAf.schedulePfxRestart(restartField.load());
                    peer.session->postEvent(FsmEvent::MAX_PREFIX_REACHED);
                }
            }
        }

        for (const auto& n : touched)
            recomputeNlri(n);
    }

    void recomputeNlri(const NlriT& nlri)
    {
        recomputeNlri(std::vector<NlriT>{nlri});
    }

    void recomputeNlri(const std::vector<NlriT> nlris)
    {
        std::vector<LocalRoute<NlriT>*> installs;
        installs.reserve(nlris.size());

        for (const auto& nlri : nlris)
        {
            std::vector<InboundRoute<NlriT>*> candidates;

            for (auto& [peer, peerTable] : adjRibIn)
            {
                for (auto& [key, route] : peerTable)
                {
                    if (key.nlri != nlri)
                        continue;
                    // Refresh IGP cost before running best-path.
                    if (route.pathId.has_value()) {
                        auto attrs = route.getPathAttributes();
                        route.igpCost = resolveIgpMetric(attrs.path.nextHop);
                    }
                    candidates.push_back(&route);
                }
            }

            // Network command: locally-originated routes count as candidates.
            {
                auto it = networkLocalRoutes.find(nlri);
                if (it != networkLocalRoutes.end())
                    candidates.push_back(&it->second);
            }

            BestPathConfig bpCfg;
            bpCfg.compareRouterId   = configs->get<Config::BgpAddressFamily::BGP_BEST_PATH_COMPARE_ROUTER_ID>().load();
            bpCfg.medMissingAsWorst = configs->get<Config::BgpAddressFamily::BGP_BEST_PATH_MED_MISSING_AS_WORST>().load();
            bpCfg.ignoreIgpMetric   = configs->get<Config::BgpAddressFamily::BGP_BEST_PATH_IGP_METRIC_IGNORE>().load();

            if (ProcessAccessor::getConfigs(process).get<Config::Bgp::BGP_DETERMINISTIC_MED>().load())
            {
                static const IPAddress kDetEmpty{};
                auto detNbr = [](const InboundRoute<NlriT>* r) -> const IPAddress& {
                    return r->sourceNeighbor ? r->sourceNeighbor->globalNbr().neighborAddress : kDetEmpty;
                };
                BestPathComparator detCmp(process, bpCfg);

                std::unordered_map<uint32_t, InboundRoute<NlriT>*> groupBest;
                for (auto* r : candidates)
                {
                    uint32_t groupAs;
                    if (!r->sourceNeighbor)
                    {
                        groupAs = 0; // locally-originated: own group (AS 0 is never valid)
                    }
                    else if (r->ebgp)
                    {
                        auto pa = r->getPathAttributes();
                        uint32_t fa = pa.attrs.firstAs();
                        groupAs = (fa != 0) ? fa : r->peerAs;
                    }
                    else
                    {
                        groupAs = r->peerAs; // iBGP / confed: group by peer AS
                    }

                    auto& gb = groupBest[groupAs];
                    if (!gb || detCmp.better(*r, detNbr(r), *gb, detNbr(gb)))
                        gb = r;
                }

                candidates.clear();
                for (auto& [_, gr] : groupBest)
                    candidates.push_back(gr);
            }

            DecisionEngine decision(process, bpCfg);
            std::optional<LocalRoute<NlriT>> best = decision.selectBest(
                candidates,
                configs->get<Config::BgpAddressFamily::MAXIMUM_PATHS_EBGP>().load(),
                configs->get<Config::BgpAddressFamily::MAXIMUM_PATHS_IBGP>().load()
            );

            // Build ADD-PATH candidate pool for additional-paths advertisement.
            if (best.has_value())
            {
                Config::Reference<Config::BgpAfBaseRegistry>& base = configs->get<Config::BgpAddressFamily::AF_BASE>().local();
                bool selectBackup    = configs->get<Config::BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BACKUP>().load();
                bool selectBestExt   = configs->get<Config::BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL>().load();
                bool selectAll       = base->get<Config::BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_ALL>().load();
                auto& selectBestFld  = base->get<Config::BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_BEST>();
                bool selectGroupBest = base->get<Config::BgpAfBase::ADVERTISE_ADDITIONAL_GROUP_BEST>().load();

                if (selectAll || selectBackup || selectBestFld.hasValue() || selectBestExt || selectGroupBest)
                {
                    std::vector<InboundRoute<NlriT>*> pool;
                    for (auto* r : decision.rankCandidates(candidates))
                    {
                        if (r == &best->route) continue;
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
                            seenAs.insert(best->route.peerAs);
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

            // BGP Route Dampening
            if (configs->get<Config::BgpAddressFamily::BGP_DAMPENING>().load())
            {
                auto now    = std::chrono::steady_clock::now();
                auto params = getDampenParams();
                auto& state = dampenTable[nlri];

                if (!best.has_value() && had)
                {
                    // Prefix transitioning reachable → unreachable: penalise.
                    state.onWithdraw(params, now);
                    if (state.suppressed)
                        startDampenReuseTimer();
                    // Fall through to normal withdrawal below.
                }
                else if (best.has_value() && !had)
                {
                    if (state.pendingReuse)
                    {
                        // Reuse timer un-suppressed this prefix; no additional penalty.
                        state.pendingReuse = false;
                    }
                    else
                    {
                        // Prefix transitioning unreachable → reachable: penalise re-announcement.
                        bool suppress = state.onAnnounce(params, now);
                        if (suppress)
                        {
                            startDampenReuseTimer();
                            return; // hold suppressed: do not install into Loc-RIB
                        }
                    }
                }
                // had && best: path-attribute change, not a reachability flap; no penalty.
            }

            if (!best.has_value())
            {
                if (had)
                {
                    withdrawFromRib(nlri);
                    locRib.erase(lit);
                    recomputeAdjRibOut(nlri, nullptr);
                    if constexpr (isIpPrefix<NlriT>)
                        scheduleAggregateRecompute();
                }
                return;
            }

            const bool bestChanged = !had || &lit->second.route != &best->route;

            // Always refresh the locRib entry so multipaths/additionalPaths stay current.
            if (had)
                locRib.erase(lit);
            locRib.emplace(nlri, *best);

            if (bestChanged)
                installs.push_back(&locRib.at(nlri));

            recomputeAdjRibOut(nlri, &locRib.at(nlri));
            if constexpr (isIpPrefix<NlriT>)
                scheduleAggregateRecompute();
        }

        installToRib(installs);
    }

    void installToRib(LocalRoute<NlriT>& route)
    {
        auto install = buildInstall(route);
        policy.installRoute(install);
        registerNht(route.route.nlri, install.attrs.path.nextHop);
    }

    void installToRib(std::vector<LocalRoute<NlriT>*>& routes)
    {
        std::vector<typename N::NlriInstall> installs;
        installs.reserve(routes.size());
        for (auto* route : routes)
            installs.push_back(buildInstall(*route));
        policy.installRoutes(installs);
        for (auto& install : installs)
            registerNht(install.route.route.nlri, install.attrs.path.nextHop);
    }

    void withdrawFromRib(const NlriT& nlri)
    {
        unregisterNht(nlri);
        policy.withdrawRoute(nlri);
    }

    void withdrawFromRib(const std::vector<NlriT>& nlri)
    {
        for (const auto& n : nlri)
            unregisterNht(n);
        policy.withdrawRoutes(nlri);
    }

    // Returns true if the route should be DROPPED (filtered out); false to accept into Adj-RIB-In.
    bool applyIngressPolicy(const InboundRoute<NlriT>& route)
    {
        PathAttribute pathAttrs = ProcessAccessor::getAttrMgr(process).get(*route.pathId);
        uint32_t routerAs = ProcessAccessor::getAsNum(process);
        auto& nbr = route.sourceNeighbor->globalNbr();

        // Route Reflector loop prevention (RFC 4456 §8): only for iBGP (not confed-eBGP).
        if (!route.ebgp && !route.confedEbgp)
        {
            const uint32_t ourRid       = ProcessAccessor::getRid(process);
            const uint32_t ourClusterId = getClusterId();

            if (pathAttrs.attrs.originatorId.has_value() &&
                *pathAttrs.attrs.originatorId == ourRid)
                return true; // ORIGINATOR_ID loop

            for (uint32_t cid : pathAttrs.attrs.clusterList)
                if (cid == ourClusterId)
                    return true; // CLUSTER_LIST loop
        }

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

            const uint32_t confedId = getConfedId();
            const bool inConfed     = (confedId != routerAs);

            uint32_t ownAsCount = 0;
            uint32_t localAsCount = 0;
            for (const auto& seg : pathAttrs.attrs.asPath)
            {
                for (uint32_t asn : seg.asns)
                {
                    if (asn == routerAs)
                        ownAsCount++;
                    // Also count confederation identifier occurrences in regular segments.
                    if (inConfed && asn == confedId &&
                        (seg.segmentType == BGP_AS_SEQUENCE || seg.segmentType == BGP_AS_SET))
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

        // the peer's configured remote AS.  Protects against misconfigured or spoofed updates.
        if (route.ebgp &&
            ProcessAccessor::getConfigs(process).get<Config::Bgp::BGP_ENFORCE_FIRST_AS>().load())
        {
            uint32_t fa = pathAttrs.attrs.firstAs();
            if (fa != 0 && fa != route.peerAs)
                return true; // reject: first-AS mismatch
        }

        // Max AS-PATH length: drop routes with an AS_PATH longer than the configured limit.
        {
            auto& maxAsField = ProcessAccessor::getConfigs(process).get<Config::Bgp::BGP_MAX_AS_LIMIT>();
            if (maxAsField.hasValue() && pathAttrs.attrs.asPathLength() > maxAsField.load())
                return true;
        }

        // Max community count: drop routes that carry too many standard communities.
        {
            auto& maxComField = ProcessAccessor::getConfigs(process).get<Config::Bgp::BGP_MAX_COMMUNITY_LIMIT>();
            if (maxComField.hasValue() && pathAttrs.attrs.communities.size() > maxComField.load())
                return true;
        }

        // Max extended community count.
        {
            auto& maxExtField = ProcessAccessor::getConfigs(process).get<Config::Bgp::BGP_MAX_EXT_COMMUNITY_LIMIT>();
            if (maxExtField.hasValue() && pathAttrs.attrs.extendedCommunities.size() > maxExtField.load())
                return true;
        }

        // TODO: ingress policy (route-maps, prefix-lists, community filters, etc.)
        return false; // accept
    }

    // Group-level egress: strip LOCAL_PREF and prepend AS-path.
    // Result is shared by all members of the same peer group + isEbgp combination.
    std::optional<PathAttribute> applyGroupEgressPolicy(const InboundRoute<NlriT>& route, const Session& session)
    {
        if (!route.pathId.has_value())
            return std::nullopt;

        PathAttribute pa = route.getPathAttributes();

        if (session.isEbgp())
        {
            auto& sesCfgs = session.getNeighbor().getConfigs();

            pa.attrs.localPref    = std::nullopt;
            pa.attrs.originatorId = std::nullopt;
            pa.attrs.clusterList.clear();

            pa.attrs.asPath.erase(
                std::remove_if(pa.attrs.asPath.begin(), pa.attrs.asPath.end(),
                    [](const AsPathSegment& s) {
                        return s.segmentType == BGP_AS_CONFED_SEQUENCE ||
                               s.segmentType == BGP_AS_CONFED_SET;
                    }),
                pa.attrs.asPath.end());

            AsPathSegment& seg = getAsSegment(pa.attrs);
            const uint32_t confedId = getConfedId();
            auto& localAs = sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_AS>();
            if (!sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS>().load() || !localAs.hasValue())
                seg.asns.insert(seg.asns.begin(), confedId);
            else if (sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_REPLACE_AS>().load())
                seg.asns.insert(seg.asns.begin(), localAs.load());
            else if (sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_NO_PREPEND>().load())
                seg.asns.insert(seg.asns.begin(), confedId);
            else
            {
                seg.asns.insert(seg.asns.begin(), confedId);
                seg.asns.insert(seg.asns.begin(), localAs.load());
            }
        }
        else if (session.isConfedEbgp())
        {
            // RR attributes; prepend local member AS as a new AS_CONFED_SEQUENCE entry.
            AsPathSegment& seg = getConfedAsSegment(pa.attrs);
            const uint32_t routerAs = ProcessAccessor::getAsNum(process);
            seg.asns.insert(seg.asns.begin(), routerAs);
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
                 route.neighborRouterId != ProcessAccessor::getRid(process)) ||
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

    struct MraiState
    {
        std::chrono::steady_clock::time_point lastSent{};
        std::unordered_set<NlriT> pending;
        uint32_t timerId = 0;
    };

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
        auto& attrMgr = ProcessAccessor::getAttrMgr(process);

        ProcessAccessor::getNtable(process).forEachNeighbor([&](Neighbor& nbr) {
            Session* session = nbr.session;
            if (!session || !session->established())
                return;

            const uint32_t peerRid = session->getPeerRid();
            PerPeerOutTable<NlriT>& peerOut = adjRibOut[peerRid];
            NeighborAf& afNbr = nbr.getAfNeighbor(family);
            auto& nbrAfCfgs = afNbr.getConfigs();

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

            // BGP_SUPPRESS_INACTIVE: suppress routes whose next-hop is not reachable via NHT.
            if (ProcessAccessor::getConfigs(process).get<Config::Bgp::BGP_SUPPRESS_INACTIVE>().load())
            {
                if (best->route.pathId.has_value() && best->route.sourceNeighbor != nullptr)
                {
                    auto paOpt = best->route.getPathAttributes();
                    auto entIt = nhtTable.find(paOpt.path.nextHop);
                    if (entIt != nhtTable.end() && !entIt->second.reachable)
                    {
                        withdrawFromPeer();
                        return;
                    }
                }
            }

            // MRAI: rate-limit announcements. Withdrawals always bypass.
            if (peerRid != mraiBypassPeer)
            {
                uint16_t mraiSecs = nbrAfCfgs.get<Config::BgpNeighbor::ADVERTISE_INTERVAL>().load();
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
                            ms.timerId = ProcessAccessor::getScheduler(process).postAfter(expiry,
                                [this, peerRid](uint32_t) { drainMraiPending(peerRid); });
                        }
                        return;
                    }
                }

                // Slow peer: defer if STATIC mode or TX buffer is backed up past detection threshold.
                {
                    auto& slowMode = nbrAfCfgs.get<Config::BgpAfBase::SLOW_PEER_MODE>();
                    bool isStatic = slowMode.hasValue() && slowMode.load() == SlowPeerMode::STATIC;

                    bool backlogged = false;
                    if (!isStatic && afNbr.isSlowPeer)
                    {
                        auto* conn = session->getPrimaryConnection();
                        backlogged = conn && conn->pendingTxBytes() > 0;
                        if (!backlogged)
                        {
                            // Recover unless DYNAMIC_PERMANENT (check config live).
                            bool permanent = slowMode.load() == SlowPeerMode::DYNAMIC_PERMANENT;
                            if (!permanent)
                            {
                                afNbr.isSlowPeer = false;
                                afNbr.slowFirstSeen = {};
                            }
                        }
                    }

                    if (isStatic || backlogged)
                    {
                        auto& ms = mraiState[peerRid];
                        ms.pending.insert(nlri);
                        if (ms.timerId == 0)
                        {
                            uint16_t interval = nbrAfCfgs.get<Config::BgpAfBase::SLOW_PEER_DETECTION_THRESHOLD>().load();
                            ms.timerId = ProcessAccessor::getScheduler(process).postAfter(
                                std::chrono::steady_clock::now() + std::chrono::seconds(interval),
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

            const bool fromIbgp = !best->route.ebgp && !best->route.confedEbgp;
            const bool toIbgp   = !session->isEbgp() && !session->isConfedEbgp();
            bool isReflecting = false;
            if (fromIbgp && toIbgp)
            {
                const bool locallyOriginated = (best->route.sourceNeighbor == nullptr);
                if (!locallyOriginated)
                {
                    const bool targetIsClient = nbrAfCfgs
                        .get<Config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>().load();
                    const bool senderIsClient = best->route.sourceNeighbor->getConfigs()
                        .template get<Config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>().load();

                    // Standard iBGP split-horizon: non-client to non-client.
                    if (!senderIsClient && !targetIsClient)
                    {
                        withdrawFromPeer();
                        return;
                    }

                    // Never reflect back to the originating client.
                    if (&best->route.sourceNeighbor->globalNbr() == &nbr)
                    {
                        withdrawFromPeer();
                        return;
                    }

                    // Client-to-client reflection: honour the global toggle.
                    if (senderIsClient && targetIsClient &&
                        !ProcessAccessor::getConfigs(process)
                            .get<Config::Bgp::BGP_CLIENT_TO_CLIENT_REFLECTION>().load())
                    {
                        withdrawFromPeer();
                        return;
                    }

                    isReflecting = true;
                }
            }

            // ACTIVATE: only exchange routes when this AF is explicitly activated for the neighbor.
            if (!afNbr.getConfigs().get<Config::BgpNeighbor::ACTIVATE>().load())
            {
                withdrawFromPeer();
                return;
            }

            // Summary-only: suppress more-specifics covered by an active aggregate.
            if constexpr (isIpPrefix<NlriT>)
            {
                if (aggregateSuppressed(nlri))
                {
                    withdrawFromPeer();
                    return;
                }
            }

            // ORF: apply peer-specified prefix-list filter on our outbound.
            if (!afNbr.orfFilter.empty() && !passesOrfFilter(nlri, afNbr.orfFilter))
            {
                withdrawFromPeer();
                return;
            }

            PeerGroup* pg = nbrAfCfgs.getPeerGroup();
            const bool addPathSend = negotiated.addPathSend(family);

            // Collect all paths to advertise based on per-neighbor ADVERTISE configs.
            std::vector<InboundRoute<NlriT>*> paths;
            paths.push_back(&best->route);

            if (addPathSend)
            {
                if (nbrAfCfgs.get<Config::BgpNeighbor::ADVERTISE_DIVERSE_PATH_MPATH>().load())
                    for (auto* mp : best->multipaths)
                        paths.push_back(mp);

                if (!best->additionalPaths.empty())
                {
                    Config::Reference<Config::BgpAfBaseRegistry>& baseCfg = configs->get<Config::BgpAddressFamily::AF_BASE>().local();
                    bool advBackup    = configs->get<Config::BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BACKUP>().load();
                    bool advBestExt   = configs->get<Config::BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL>().load();
                    bool advAll       = baseCfg->get<Config::BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_ALL>().load();
                    auto& advBestFld  = baseCfg->get<Config::BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_BEST>();
                    bool advGroupBest = baseCfg->get<Config::BgpAfBase::ADVERTISE_ADDITIONAL_GROUP_BEST>().load();

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

                // Route Reflector (RFC 4456 §8): stamp ORIGINATOR_ID and prepend CLUSTER_LIST.
                if (isReflecting)
                {
                    if (!egressAttrs->attrs.originatorId.has_value())
                        egressAttrs->attrs.originatorId = route->neighborRouterId;
                    egressAttrs->attrs.clusterList.insert(
                        egressAttrs->attrs.clusterList.begin(), getClusterId());
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

            // Slow peer detection (DYNAMIC / DYNAMIC_PERMANENT): update state from TX backlog.
            if (!afNbr.isSlowPeer)
            {
                bool detectEnabled = nbrAfCfgs.get<Config::BgpAfBase::SLOW_PEER_DETECTION>().load();
                if (detectEnabled)
                {
                    auto* conn = session->getPrimaryConnection();
                    auto now = std::chrono::steady_clock::now();
                    if (conn && conn->pendingTxBytes() > 0)
                    {
                        if (afNbr.slowFirstSeen == std::chrono::steady_clock::time_point{})
                            afNbr.slowFirstSeen = now;

                        uint16_t thresh = nbrAfCfgs.get<Config::BgpAfBase::SLOW_PEER_DETECTION_THRESHOLD>().load();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - afNbr.slowFirstSeen);
                        if (elapsed.count() >= thresh)
                            afNbr.isSlowPeer = true;
                    }
                    else
                    {
                        afNbr.slowFirstSeen = {};
                    }
                }
            }
        });
    }

    uint64_t resolveIgpMetric(const IPAddress& nextHop) const
    {
        if (!igpMetricResolver)
            return std::numeric_limits<uint64_t>::max();
        return igpMetricResolver(nextHop);
    }

    uint32_t getClusterId() const
    {
        auto& cidField = ProcessAccessor::getConfigs(process).get<Config::Bgp::BGP_CLUSTER_ID>();
        return cidField.hasValue() ? cidField.load() : ProcessAccessor::getRid(process);
    }

    uint32_t getConfedId() const
    {
        auto& cidField = ProcessAccessor::getConfigs(process).get<Config::Bgp::BGP_CONFEDERATION_IDENTIFIER>();
        return cidField.hasValue() ? cidField.load() : ProcessAccessor::getAsNum(process);
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
                if (!e.prefix.contains(nlri))
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

    struct AggregateState
    {
        bool active = false;
        PathAttribute basePa;
    };

public:
    void sendDefaultOriginate(Session& session)
    {
        if constexpr (!isIpPrefix<NlriT>)
            return;

        Neighbor& nbr = session.getNeighbor();
        NeighborAf& afNbr = nbr.getAfNeighbor(family);

        if (!afNbr.getConfigs().get<Config::BgpNeighbor::ACTIVATE>().load())
            return;
        if (!afNbr.getConfigs().get<Config::BgpAfBase::DEFAULT_ORIGINATE>().load())
            return;

        NlriT defaultNlri{};
        if constexpr (requires { defaultNlri.af; })
        {
            if constexpr (N::afi.afi == BGP_AFI_IPV6)
                defaultNlri.af = ::AddressFamily::IPv6;
            else
                defaultNlri.af = ::AddressFamily::IPv4;
        }

        const bool isEbgp     = session.isEbgp();
        const bool isConfedEbgp = session.isConfedEbgp();
        const uint32_t routerAs = ProcessAccessor::getAsNum(process);
        auto& sesCfgs = nbr.getConfigs();

        PathAttribute pa{};
        pa.attrs.origin = BGP_ORIGIN_IGP;

        if (isEbgp)
        {
            AsPathSegment seg;
            seg.segmentType = BGP_AS_SEQUENCE;

            bool localAsEnabled = sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS>().load();
            auto& localAsField  = sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_AS>();
            const uint32_t confedId = getConfedId();

            if (!localAsEnabled || !localAsField.hasValue())
                seg.asns.push_back(confedId);
            else if (sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_REPLACE_AS>().load())
                seg.asns.push_back(localAsField.load());
            else if (sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_NO_PREPEND>().load())
                seg.asns.push_back(confedId);
            else
            {
                seg.asns.push_back(localAsField.load());
                seg.asns.push_back(confedId);
            }

            pa.attrs.asPath.push_back(std::move(seg));
        }
        else if (isConfedEbgp)
        {
            // Default-originate to a confederation peer: AS_CONFED_SEQUENCE with member AS.
            AsPathSegment seg;
            seg.segmentType = BGP_AS_CONFED_SEQUENCE;
            seg.asns.push_back(routerAs);
            pa.attrs.asPath.push_back(std::move(seg));
            pa.attrs.localPref = 100;
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
        if constexpr (!isIpPrefix<NlriT>)
            return;

        const uint32_t peerRid = session.getPeerRid();
        if (!defaultOriginatedPeers.erase(peerRid))
            return;

        NlriT defaultNlri{};
        if constexpr (requires { defaultNlri.af; })
        {
            if constexpr (N::afi.afi == BGP_AFI_IPV6)
                defaultNlri.af = ::AddressFamily::IPv6;
            else
                defaultNlri.af = ::AddressFamily::IPv4;
        }

        BuildUpdate<NlriT> withdraw;
        withdraw.withdrawn.push_back({defaultNlri, 0});
        session.sendUpdate<N>(withdraw);
    }

private:
    void sendActiveAggregatesToPeer(Session& session)
    {
        if constexpr (!isIpPrefix<NlriT>)
            return;
        if (!session.getNegotiated().activeFamilies.count(family))
            return;
        if (!session.getNeighbor().getAfNeighbor(family).getConfigs().get<Config::BgpNeighbor::ACTIVATE>().load())
            return;

        for (auto& [aggNlri, state] : aggregateStates)
            if (state.active)
                sendAggregateToPeer(aggNlri, state, session);
    }

    bool aggregateSuppressed(const NlriT& nlri)
    {
        bool suppressed = false;
        configs->get<Config::BgpAddressFamily::AGGREGATE_ADDRESS>().withRead([&](const auto& aggCfgs) {
            for (const auto& aggCfg : aggCfgs)
            {
                if (!Config::BgpAggregateAddress::summaryOnly(aggCfg))
                    continue;
                const NlriT aggNlri = Config::BgpAggregateAddress::prefix(aggCfg);
                auto stateIt = aggregateStates.find(aggNlri);
                if (stateIt == aggregateStates.end() || !stateIt->second.active)
                    continue;
                if (nlri.prefixLength > aggNlri.prefixLength && aggNlri.contains(nlri))
                {
                    suppressed = true;
                    return;
                }
            }
        });
        return suppressed;
    }

    void scheduleAggregateRecompute()
    {
        if (aggregateTimerId != 0)
            return;
        uint16_t delay = configs->get<Config::BgpAddressFamily::BGP_AGGREGATE_TIMER>().load();
        auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(delay);
        aggregateTimerId = ProcessAccessor::getScheduler(process).postAfter(expiry,
            [this](uint32_t) { aggregateTimerId = 0; recomputeAllAggregates(); });
    }

    void recomputeAllAggregates()
    {
        if constexpr (!isIpPrefix<NlriT>)
            return;

        std::vector<Config::BgpAggregateAddress::Tuple> cfgs;
        configs->get<Config::BgpAddressFamily::AGGREGATE_ADDRESS>().withRead([&](const auto& v) {
            cfgs = v;
        });

        for (auto it = aggregateStates.begin(); it != aggregateStates.end(); )
        {
            bool found = std::any_of(cfgs.begin(), cfgs.end(), [&](const auto& cfg) {
                return Config::BgpAggregateAddress::prefix(cfg) == it->first;
            });
            if (!found)
            {
                if (it->second.active)
                    withdrawAggregate(it->first);
                it = aggregateStates.erase(it);
            }
            else
                ++it;
        }

        for (const auto& cfg : cfgs)
            recomputeAggregate(cfg);
    }

    void recomputeAggregate(const Config::BgpAggregateAddress::Tuple& cfg)
    {
        const NlriT aggNlri = Config::BgpAggregateAddress::prefix(cfg);
        const bool buildAsSet = Config::BgpAggregateAddress::asConfedSet(cfg);

        std::vector<const InboundRoute<NlriT>*> contributors;
        for (const auto& [nlri, route] : locRib)
        {
            if (nlri.prefixLength <= aggNlri.prefixLength)
                continue;
            if (!aggNlri.contains(nlri))
                continue;
            contributors.push_back(&route.route);
        }

        AggregateState& state = aggregateStates[aggNlri];

        if (contributors.empty())
        {
            if (state.active)
            {
                withdrawAggregate(aggNlri);
                state.active = false;
            }
            return;
        }

        PathAttribute pa{};
        pa.path.family = family;

        uint8_t worstOrigin = BGP_ORIGIN_IGP;
        for (auto* r : contributors)
        {
            if (!r->pathId.has_value()) continue;
            auto rpa = r->getPathAttributes();
            if (rpa.attrs.origin.has_value() && *rpa.attrs.origin > worstOrigin)
                worstOrigin = *rpa.attrs.origin;
        }
        pa.attrs.origin = worstOrigin;

        if (buildAsSet)
        {
            const uint32_t localAs = ProcessAccessor::getAsNum(process);
            std::unordered_set<uint32_t> asns;
            for (auto* r : contributors)
            {
                if (!r->pathId.has_value()) continue;
                auto rpa = r->getPathAttributes();
                for (const auto& seg : rpa.attrs.asPath)
                    for (uint32_t asn : seg.asns)
                        if (asn != localAs)
                            asns.insert(asn);
            }
            if (!asns.empty())
            {
                AsPathSegment seg;
                seg.segmentType = BGP_AS_SET;
                seg.asns = std::vector<uint32_t>(asns.begin(), asns.end());
                pa.attrs.asPath.push_back(std::move(seg));
            }
        }
        else
        {
            pa.attrs.atomicAggregate = true;
        }

        {
            Aggregator agg;
            agg.asn = ProcessAccessor::getAsNum(process);
            agg.speaker = IPAddress(ProcessAccessor::getRid(process));
            pa.attrs.asAggregator = std::move(agg);
        }

        state.basePa = pa;
        sendAggregateToAllPeers(aggNlri, state);
        state.active = true;
    }

    void applyAggregateEgressPolicy(PathAttribute& pa, const Session& session)
    {
        auto& sesCfgs = session.getNeighbor().getConfigs();

        if (session.isEbgp())
        {
            pa.attrs.localPref    = std::nullopt;
            pa.attrs.originatorId = std::nullopt;
            pa.attrs.clusterList.clear();

            // Strip CONFED segments before advertising to real eBGP.
            pa.attrs.asPath.erase(
                std::remove_if(pa.attrs.asPath.begin(), pa.attrs.asPath.end(),
                    [](const AsPathSegment& s) {
                        return s.segmentType == BGP_AS_CONFED_SEQUENCE ||
                               s.segmentType == BGP_AS_CONFED_SET;
                    }),
                pa.attrs.asPath.end());

            AsPathSegment& seg = getAsSegment(pa.attrs);
            const uint32_t confedId = getConfedId();
            auto& localAsField = sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_AS>();
            if (!sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS>().load() || !localAsField.hasValue())
                seg.asns.insert(seg.asns.begin(), confedId);
            else if (sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_REPLACE_AS>().load())
                seg.asns.insert(seg.asns.begin(), localAsField.load());
            else if (sesCfgs.get<Config::BgpNeighborSession::LOCAL_AS_NO_PREPEND>().load())
                seg.asns.insert(seg.asns.begin(), confedId);
            else
            {
                seg.asns.insert(seg.asns.begin(), confedId);
                seg.asns.insert(seg.asns.begin(), localAsField.load());
            }
        }
        else if (session.isConfedEbgp())
        {
            // Prepend member AS as AS_CONFED_SEQUENCE; keep LOCAL_PREF.
            AsPathSegment& seg = getConfedAsSegment(pa.attrs);
            seg.asns.insert(seg.asns.begin(), ProcessAccessor::getAsNum(process));
            pa.attrs.localPref = 100;
        }
        else
        {
            pa.attrs.localPref = 100;
        }

        if (auto* conn = session.getPrimaryConnection())
            pa.path.nextHop = conn->socketKey()->local.address;
    }

    void sendAggregateToAllPeers(const NlriT& aggNlri, AggregateState& state)
    {
        ProcessAccessor::getNtable(process).forEachNeighbor([&](Neighbor& nbr) {
            Session* session = nbr.session;
            if (!session || !session->established())
                return;
            if (!session->getNegotiated().activeFamilies.count(family))
                return;
            if (!nbr.getAfNeighbor(family).getConfigs().get<Config::BgpNeighbor::ACTIVATE>().load())
                return;

            sendAggregateToPeer(aggNlri, state, *session);
        });
    }

    void sendAggregateToPeer(const NlriT& aggNlri, AggregateState& state, Session& session)
    {
        PathAttribute pa = state.basePa;
        applyAggregateEgressPolicy(pa, session);

        BuildUpdate<NlriT> update;
        typename BuildUpdate<NlriT>::Announcement ann;
        ann.attrs = std::move(pa);
        ann.nlri.push_back({aggNlri, 0});
        update.announcements.push_back(std::move(ann));
        session.sendUpdate<N>(update);
    }

    void withdrawAggregate(const NlriT& aggNlri)
    {
        ProcessAccessor::getNtable(process).forEachNeighbor([&](Neighbor& nbr) {
            Session* session = nbr.session;
            if (!session || !session->established())
                return;
            if (!session->getNegotiated().activeFamilies.count(family))
                return;

            BuildUpdate<NlriT> withdraw;
            withdraw.withdrawn.push_back({aggNlri, 0});
            session->sendUpdate<N>(withdraw);
        });
    }

    DampenParams getDampenParams() const
    {
        DampenParams p;
        p.halfLifeSecs    = configs->get<Config::BgpAddressFamily::BGP_DAMPENING_HALF_LIFE>().load() * 60.0;
        p.reuse           = configs->get<Config::BgpAddressFamily::BGP_DAMPENING_REUSE_THRESHOLD>().load();
        p.suppress        = configs->get<Config::BgpAddressFamily::BGP_DAMPENING_SUPPRESS_THRESHOLD>().load();
        p.maxSuppressSecs = configs->get<Config::BgpAddressFamily::BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME>().load() * 60.0;
        p.ceiling         = p.suppress * std::pow(2.0, p.maxSuppressSecs / p.halfLifeSecs);
        return p;
    }

    void startDampenReuseTimer()
    {
        if (dampenReuseTimerId_ != 0)
            return;
        dampenReuseTimerId_ = ProcessAccessor::getScheduler(process).postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(5),
            [this](uint32_t) { dampenReuseTimerId_ = 0; onDampenReuseTimer(); });
    }

    void onDampenReuseTimer()
    {
        auto params = getDampenParams();
        auto now    = std::chrono::steady_clock::now();

        bool anySuppressed = false;
        std::vector<NlriT> toReuse;

        for (auto it = dampenTable.begin(); it != dampenTable.end(); )
        {
            auto& [nlri, state] = *it;

            if (state.checkReuse(params, now))
            {
                state.pendingReuse = true;
                toReuse.push_back(nlri);
            }

            if (state.suppressed)
                anySuppressed = true;

            if (state.isStale())
                it = dampenTable.erase(it);
            else
                ++it;
        }

        for (const NlriT& nlri : toReuse)
            recomputeNlri(nlri);

        if (anySuppressed)
            startDampenReuseTimer();
    }

    struct NhtCtx
    {
        AddressFamilyInstance* self;
        IPAddress              nextHop;
        ProcessQueueRef        bgpSched;
    };

    struct NhtEntry
    {
        uint32_t                  watchId   = 0;
        bool                      isV6      = false;
        bool                      reachable = true;
        std::unordered_set<NlriT> nlris;
        std::optional<NhtCtx>     ctx;
    };

    template <typename Addr>
    static bool nhtCallback(RouteWatcher<Addr>::CallbackCtx& cctx)
    {
        auto* ntx     = static_cast<NhtCtx*>(cctx.ctx);
        bool  reach   = (cctx.newBest != nullptr);
        IPAddress nh  = ntx->nextHop;
        ntx->bgpSched.post([self = ntx->self, nh, reach]() {
            self->onNhtChange(nh, reach);
        });
        return false;
    }

    void registerNht(const NlriT& nlri, const IPAddress& nh)
    {
        if (!configs->get<Config::BgpAddressFamily::BGP_NEXT_HOP_TRACKING>().load())
            return;

        auto& rt = ProcessAccessor::getRoutingInstance(process).getRib();

        auto [it, inserted] = nhtTable.emplace(nh, NhtEntry{});
        NhtEntry& entry = it->second;
        entry.nlris.insert(nlri);

        if (inserted)
        {
            entry.isV6 = nh.isIPv6();
            entry.ctx.emplace(NhtCtx{this, nh, ProcessAccessor::getScheduler(process)});
            if (nh.isIPv6())
                entry.watchId = rt.watchAddress(nh.v6(), &entry.ctx.value(), nhtCallback<__uint128_t>);
            else
                entry.watchId = rt.watchAddress(nh.v4(), &entry.ctx.value(), nhtCallback<uint32_t>);
        }

        nlriToNextHop[nlri] = nh;
    }

    void unregisterNht(const NlriT& nlri)
    {
        auto nhIt = nlriToNextHop.find(nlri);
        if (nhIt == nlriToNextHop.end()) return;

        IPAddress nh = nhIt->second;
        nlriToNextHop.erase(nhIt);

        auto entIt = nhtTable.find(nh);
        if (entIt == nhtTable.end()) return;

        NhtEntry& entry = entIt->second;
        entry.nlris.erase(nlri);

        if (entry.nlris.empty())
        {
            if (entry.watchId)
            {
                auto& rt = ProcessAccessor::getRoutingInstance(process).getRib();
                rt.unwatchAddress(entry.watchId, entry.isV6);
            }
            nhtTable.erase(entIt);
        }
    }

    void clearNhtWatches()
    {
        if (nhtTable.empty()) return;
        auto& rt = ProcessAccessor::getRoutingInstance(process).getRib();
        for (auto& [nh, entry] : nhtTable)
            if (entry.watchId)
                rt.unwatchAddress(entry.watchId, entry.isV6);
        nhtTable.clear();
        nlriToNextHop.clear();
    }

    uint8_t computeAdminDistance(const InboundRouteBase& route, const NlriT& nlri) const
    {
        // MBGP = any AF other than IPv4 unicast.
        constexpr bool isMbgp =
            !(N::afi.afi == BGP_AFI_IPV4 && N::afi.safi == BGP_SAFI_UNICAST);

        uint8_t dist;
        if (route.sourceNeighbor == nullptr)
        {
            // Locally-originated (network command)
            dist = isMbgp
                ? configs->get<Config::BgpAddressFamily::DISTANCE_MBGP_LOCAL>().load()
                : configs->get<Config::BgpAddressFamily::DISTANCE_BGP_LOCAL>().load();
        }
        else if (route.ebgp)
        {
            dist = isMbgp
                ? configs->get<Config::BgpAddressFamily::DISTANCE_MBGP_EXTERNAL>().load()
                : configs->get<Config::BgpAddressFamily::DISTANCE_BGP_EXTERNAL>().load();
        }
        else
        {
            dist = isMbgp
                ? configs->get<Config::BgpAddressFamily::DISTANCE_MBGP_INTERNAL>().load()
                : configs->get<Config::BgpAddressFamily::DISTANCE_BGP_INTERNAL>().load();
        }

        // DISTANCE_RANGE: per-prefix administrative distance override.
        if constexpr (isIpPrefix<NlriT>)
        {
            configs->get<Config::BgpAddressFamily::DISTANCE_RANGE>().withRead(
                [&](const auto& ranges)
                {
                    for (const auto& range : ranges)
                    {
                        uint8_t rangeDist       = std::get<0>(range);
                        const auto& pfxList     = std::get<1>(range);
                        IPPrefix nlriAsPfx(nlri.addr, nlri.prefixLength);
                        for (const auto& [pfx, pfxListName] : pfxList)
                        {
                            if (pfx == nlriAsPfx ||
                                (pfx.prefixLength <= nlri.prefixLength && pfx.contains(nlriAsPfx)))
                            {
                                dist = rangeDist;
                                return;
                            }
                        }
                    }
                });
        }

        return dist;
    }

    typename N::NlriInstall buildInstall(LocalRoute<NlriT>& route)
    {
        auto pa = route.route.getPathAttributes();

        typename N::NlriInstall install = {
            .route = route,
            .attrs = pa
        };

        const NlriT& nlri   = route.route.nlri;
        install.distance = computeAdminDistance(route.route, nlri);
        install.metric = 0;

        if (pa.attrs.med.has_value())
        {
            install.metric = *pa.attrs.med;
        }
        else if (route.route.sourceNeighbor == nullptr)
        {
            // Locally-originated route: apply DEFAULT_METRIC when no MED is set.
            auto& defMetricField = configs->get<Config::BgpAddressFamily::DEFAULT_METRIC>();
            if (defMetricField.hasValue())
                install.metric = defMetricField.load();
        }

        install.recursiveHost = configs->get<Config::BgpAddressFamily::BGP_RECURSIVE_HOST>().load();

        return install;
    }

    void withdrawFromRibDirect(const NlriT& nlri)
    {
        if constexpr (!isIpPrefix<NlriT>)
            return;

        auto& rt           = ProcessAccessor::getRoutingInstance(process).getRib();
        const uint32_t pid = ProcessAccessor::getAsNum(process);

        if constexpr (N::afi.afi == BGP_AFI_IPV4)
            rt.removeRoute(readU32(nlri.addr), nlri.prefixLength, RouteSource::BGP, pid);
        else if constexpr (N::afi.afi == BGP_AFI_IPV6)
            rt.removeRoute(readU128(nlri.addr), nlri.prefixLength, RouteSource::BGP, pid);
    }

    struct NetworkWatchCtx
    {
        AddressFamilyInstance* self;
        NlriT                  nlri;
        ProcessQueueRef        bgpSched;
    };

    struct NetworkWatchEntry
    {
        uint32_t                   watchId = 0;
        bool                       isV6    = false;
        std::optional<NetworkWatchCtx> ctx;
    };

    template <typename Addr>
    static bool networkWatchCallback(typename RouteWatcher<Addr>::CallbackCtx& cctx)
    {
        auto* ctx  = static_cast<NetworkWatchCtx*>(cctx.ctx);
        bool reach = (cctx.newBest != nullptr);
        NlriT nlri = ctx->nlri;
        ctx->bgpSched.post([self = ctx->self, nlri, reach]() {
            self->onNetworkRibChanged(nlri, reach);
        });
        return false; // keep watch alive
    }

    // Called on the BGP scheduler when a watched network prefix appears/disappears in the RIB.
    void onNetworkRibChanged(const NlriT& nlri, bool reachable)
    {
        if (reachable)
        {
            auto& attrMgr = ProcessAccessor::getAttrMgr(process);

            PathAttribute pa{};
            pa.attrs.origin  = BGP_ORIGIN_IGP;
            pa.path.family   = family;

            // Apply DEFAULT_METRIC for locally-originated routes.
            auto& defMetricField = configs->get<Config::BgpAddressFamily::DEFAULT_METRIC>();
            if (defMetricField.hasValue())
                pa.attrs.med = defMetricField.load();

            uint32_t pid = attrMgr.acquire(pa.attrs, pa.path);

            networkLocalRoutes.erase(nlri);
            networkLocalRoutes.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(nlri),
                std::forward_as_tuple(attrMgr, pid, nlri, static_cast<NeighborAf*>(nullptr)));
        }
        else
        {
            networkLocalRoutes.erase(nlri);
        }

        recomputeNlri(nlri);
    }

    // Synchronise network-command watches with the current NETWORK config.
    // Call once at construction and whenever the NETWORK config changes.
    void syncNetworkRoutes()
    {
        if constexpr (!isIpPrefix<NlriT>)
            return;

        // Snapshot the currently configured prefixes.
        std::vector<NlriT> configured;
        configs->get<Config::BgpAddressFamily::NETWORK>().withRead(
            [&](const auto& nets)
            {
                for (const auto& net : nets)
                    configured.push_back(std::get<0>(net));
            });

        auto& rt = ProcessAccessor::getRoutingInstance(process).getRib();

        // Remove watches for prefixes no longer in the config.
        for (auto it = networkWatches.begin(); it != networkWatches.end(); )
        {
            bool found = std::any_of(configured.begin(), configured.end(),
                [&](const NlriT& p) { return p == it->first; });
            if (!found)
            {
                if (it->second.watchId)
                    rt.unwatchAddress(it->second.watchId, it->second.isV6);
                NlriT staleNlri = it->first;
                it = networkWatches.erase(it);

                if (networkLocalRoutes.erase(staleNlri))
                    recomputeNlri(staleNlri);
            }
            else
            {
                ++it;
            }
        }

        // Add watches for newly configured prefixes.
        for (const NlriT& pfx : configured)
        {
            if (networkWatches.count(pfx))
                continue;

            auto [watchIt, inserted] = networkWatches.emplace(pfx, NetworkWatchEntry{});
            if (!inserted)
                continue;

            NetworkWatchEntry& entry = watchIt->second;
            entry.ctx.emplace(NetworkWatchCtx{this, pfx, ProcessAccessor::getScheduler(process)});

            if constexpr (N::afi.afi == BGP_AFI_IPV6)
            {
                entry.isV6    = true;
                entry.watchId = rt.watchRoute(readU128(pfx.addr), pfx.prefixLength,
                                              &entry.ctx.value(), networkWatchCallback<__uint128_t>);
            }
            else
            {
                entry.isV6    = false;
                entry.watchId = rt.watchRoute(readU32(pfx.addr), pfx.prefixLength,
                                              &entry.ctx.value(), networkWatchCallback<uint32_t>);
            }
        }
    }

    void clearNetworkWatches()
    {
        if (networkWatches.empty())
            return;

        auto& rt = ProcessAccessor::getRoutingInstance(process).getRib();
        for (auto& [nlri, entry] : networkWatches)
            if (entry.watchId)
                rt.unwatchAddress(entry.watchId, entry.isV6);

        networkWatches.clear();
        networkLocalRoutes.clear();
    }

    void onNhtChange(const IPAddress& nh, bool reachable)
    {
        auto it = nhtTable.find(nh);
        if (it == nhtTable.end()) return;

        NhtEntry& entry = it->second;
        if (entry.reachable == reachable) return;
        entry.reachable = reachable;

        for (const NlriT& nlri : entry.nlris)
            pendingNhtRecompute.insert(nlri);

        scheduleNhtRecompute();
    }

    void scheduleNhtRecompute()
    {
        if (nhtTimerId != 0) return;

        auto& delayField = configs->get<Config::BgpAddressFamily::BGP_NEXT_HOP_TRIGGER_DELAY>();
        uint16_t delaySecs = delayField.hasValue() ? delayField.load() : 5;

        nhtTimerId = ProcessAccessor::getScheduler(process).postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(delaySecs),
            [this](uint32_t) { nhtTimerId = 0; processNhtPending(); });
    }

    void processNhtPending()
    {
        std::unordered_set<NlriT> pending = std::move(pendingNhtRecompute);
        for (const NlriT& nlri : pending)
            recomputeNlri(nlri);
    }

    // Purge routes from Adj-RIB-In that are still in the stale set after BORR/EORR.
    void purgeStalePeer(uint32_t peerRid)
    {
        auto staleIt = stalePeerNlris.find(peerRid);
        if (staleIt == stalePeerNlris.end() || staleIt->second.empty())
            return;

        auto inIt = adjRibIn.find(peerRid);
        if (inIt != adjRibIn.end())
        {
            for (auto it = inIt->second.begin(); it != inIt->second.end(); )
            {
                if (staleIt->second.count(it->first.nlri))
                {
                    auto locIt = locRib.find(it->first.nlri);
                    if (locIt != locRib.end() && &locIt->second.route == &it->second)
                        locRib.erase(locIt);
                    it = inIt->second.erase(it);
                }
                else
                    ++it;
            }
        }

        std::unordered_set<NlriT> touched = std::move(staleIt->second);
        stalePeerNlris.erase(staleIt);
        for (const NlriT& nlri : touched)
            recomputeNlri(nlri);
    }

    void cancelStaleTimers(uint32_t peerRid)
    {
        auto it = staleTimers.find(peerRid);
        if (it == staleTimers.end()) return;

        auto sched = ProcessAccessor::getScheduler(process);
        if (it->second.stalepath) sched.cancel(it->second.stalepath);
        if (it->second.maxEor)    sched.cancel(it->second.maxEor);
        staleTimers.erase(it);
        stalePeerNlris.erase(peerRid);
    }

private:

    // Pre-policy Adj-RIB-In for soft-reconfiguration inbound.
    PreAdjRibInTable<NlriT> preAdjRibIn;
    AdjRibInTable<NlriT>  adjRibIn;
    N::LocRib locRib;
    AdjRibOutTable<NlriT> adjRibOut;

    std::unordered_map<uint32_t, MraiState> mraiState;
    uint32_t mraiBypassPeer = 0;

    std::unordered_map<NlriT, DampenState> dampenTable;
    uint32_t dampenReuseTimerId_ = 0;

    std::unordered_set<uint32_t> defaultOriginatedPeers;
    std::unordered_map<NlriT, AggregateState> aggregateStates;
    uint32_t aggregateTimerId = 0;

    std::unordered_map<IPAddress, NhtEntry>  nhtTable;
    std::unordered_map<NlriT,    IPAddress>  nlriToNextHop;
    std::unordered_set<NlriT>                pendingNhtRecompute;
    uint32_t                                 nhtTimerId = 0;

    // Network command: locally-originated route state.
    std::unordered_map<NlriT, NetworkWatchEntry>     networkWatches;
    std::unordered_map<NlriT, InboundRoute<NlriT>>   networkLocalRoutes;

    // Enhanced Route Refresh (RFC 7313) stale-path tracking.
    struct StaleTimerIds { uint32_t stalepath = 0; uint32_t maxEor = 0; };
    std::unordered_map<uint32_t, StaleTimerIds>              staleTimers;
    std::unordered_map<uint32_t, std::unordered_set<NlriT>>  stalePeerNlris;

    N policy;
    IgpMetricResolver igpMetricResolver;
    BgpProcess& process;
    AfiSafi family;

    Config::Reference<Config::BgpAddressFamilyRegistry> configs;
};
}

#endif // BGP_ADDRESS_FAMILY_H
