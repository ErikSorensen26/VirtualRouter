/**
 * @file AddressFamilyInstance.h
 * @brief Per-AFI route processing instance: Adj-RIB-In, Adj-RIB-Out, Loc-RIB management.
 */

/**
 * @defgroup BGP_AF BGP Address Families
 * @ingroup BGP
 * @brief Per-AFI instances, NLRI policies, address-family logic, and scope accessor.
 */

#ifndef BGP_ADDRESS_FAMILY_INSTANCE_H
#define BGP_ADDRESS_FAMILY_INSTANCE_H

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <IPAddress.h>
#include <VirtualRouter.h>

#include "ScopeAccessor.h"
#include "configs/FieldAccessor.hpp"
#include "bgp/BgpTypes.hpp"
#include "bgp/session/Session.h"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/transport/BgpRx.h"
#include "bgp/transport/BgpTx.h"
#include "bgp/decision/DecisionEngine.hpp"
#include "bgp/neighbor/NeighborAf.h"
#include "bgp/attributes/AttributeManager.hpp"
#include "bgp/af/DirtyState.hpp"
#include "bgp/af/EgressPolicy.hpp"

namespace routing::bgp
{

/**
 * @brief Serializes and sends a BGP UPDATE to the peer over the primary TCP connection.
 *
 * Defined here (not in Session.cpp) because it requires BgpTx::buildUpdate<N>,
 * which is a template that must be instantiated in the same translation unit
 * as AddressFamilyInstance.
 *
 * @tparam N  NLRI policy type; governs how the UPDATE is serialized.
 * @param update  Routes to announce and withdraw.
 */
template <typename N>
inline void Session::sendUpdate(const BuildUpdate<typename N::Nlri>& update)
{
    if (primaryConn)
    {
        BgpTx::buildUpdate<N>(*primaryConn, *this, update);
        primaryConn->flush();
    }
}

/**
 * @brief Non-template handle for an @ref AddressFamilyInstance.
 * @ingroup BGP_AF
 *
 * The config registry fires plain `void(*)(void*)` applier callbacks (see
 * BgpRegistry.cpp) that carry the owning object as a `void*`. Because
 * @ref AddressFamilyInstance is a template, callbacks cannot name it directly; they
 * cast the `void*` to this stable base instead. The address-family instance registers
 * itself as its process-AF config context as an `AfInstanceBase*`, so the pointer the
 * callback receives is always upcastable here.
 *
 * Every entry point simply marks a dirty category and arms the debounce timer; the
 * actual recompute happens later in @ref AddressFamilyInstance::flushDirty.
 */
struct AfInstanceBase
{
    virtual ~AfInstanceBase() = default;

    /// Mark an inbound-pipeline category dirty for @p peerRid (0 = all peers).
    virtual void markInboundDirty(InDirty category, uint32_t peerRid = 0) = 0;
    /// Record that @p peerRid's NeighborAf has dirty outbound attributes, and arm the flush.
    virtual void markNeighborOutDirty(uint32_t peerRid) = 0;
    /// Mark a process-AF (Loc-RIB / decision) category dirty.
    virtual void markAfDirty(AfDirty category) = 0;

    /// Post @ref markAfDirty onto the BGP scheduler.
    virtual void enqueueMarkAfDirty(AfDirty category) = 0;

    /// Reconcile network-statement RIB watches after the NETWORK list changed.
    virtual void enqueueSyncNetwork() = 0;
};

/**
 * @brief Per-AFI route processing instance: Adj-RIB-In, Loc-RIB, and Adj-RIB-Out for one address family.
 * @ingroup BGP_AF
 *
 * Owns and operates all three RIBs for a single BGP address family (IPv4 unicast,
 * IPv6 unicast, VPN, etc.). Each instance:
 * - Receives parsed UPDATE messages from @ref BgpRx and applies inbound policy.
 * - Runs the @ref DecisionEngine to select the best path (and ECMP peers).
 * - Installs winning routes into the VRF routing table via the NLRI policy.
 * - Maintains Adj-RIB-Out per peer and drives UPDATE generation through @ref BgpTx.
 *
 * Additional features per instance:
 * - Minimum Route Advertisement Interval (MRAI) per peer
 * - Enhanced Route Refresh / stale-path tracking (RFC 7313)
 * - Next-Hop Tracking (NHT) with RIB watch callbacks
 * - Aggregate address generation with summary-only suppression
 * - Default-originate to configured peers
 * - ADD-PATH advertisement (RFC 7911)
 * - Outbound Route Filtering (ORF) via peer-sent prefix lists
 * - Soft-reconfiguration inbound (pre-policy RIB storage)
 * - Slow-peer detection and deferral
 *
 * ## Architectural Role
 * Sits between BGP sessions and the VRF routing table. It is the decision
 * boundary: everything inbound is normalised here and the best result is
 * written to both the RIB and Adj-RIB-Out. One instance exists per enabled
 * (BgpScope, AFI/SAFI) combination.
 *
 * ## Lifecycle & Ownership
 * Owned by BgpScope (stored in its AFI map). Constructed when the AFI is
 * enabled; destroyed when the AFI is disabled or the scope stops. The
 * destructor cancels all active timers and removes all RIB watches before
 * releasing memory.
 *
 * ## Concurrency Model
 * All methods run exclusively on the BgpScope scheduler thread. The RIB
 * watch callbacks post back to this scheduler before touching any member
 * state, so no internal locking is required.
 *
 * @tparam N  NLRI policy type. Must provide:
 *              - `N::Nlri`        — prefix type (e.g. types::IPv4Prefix)
 *              - `N::afi`         — AfiSafi constant identifying this family
 *              - `N::LocRib`      — LocRib storage backend type
 *              - `N::NlriInstall` — route install descriptor type
 *              - `installRoute`, `installRoutes`, `withdrawRoute`, `withdrawRoutes`
 *                methods matching the NlriPolicy interface
 *
 * @see BgpScope, Session, DecisionEngine, NlriPolicy, LocRib
 */
template <typename N>
class AddressFamilyInstance : public AfInstanceBase
{
public:
    static constexpr AfiSafi afi = N::afi; ///< AFI/SAFI constants identifying this address family.

    static constexpr uint16_t kDefaultEbgpMraiSecs = 30;
    static constexpr uint16_t kDefaultIbgpMraiSecs = 5;

    using NlriT = typename N::Nlri;        ///< Prefix type for this AFI (e.g. types::IPv4Prefix).
    using IgpMetricResolver = std::function<uint64_t(const types::IPAddress&)>; ///< Callable that resolves the IGP cost to a next-hop address.

    /**
     * @brief Constructs the AFI instance, registers AF-base config, and starts network-command watches.
     *
     * Initialises all RIBs empty. The IGP metric resolver defaults to returning
     * max uint64 (unreachable) until overridden via setIgpMetricResolver. Config
     * is loaded lazily from the registry; syncNetworkRoutes() fires immediately
     * to install any pre-configured `network` prefixes.
     *
     * @param scope  Owning BGP scope; provides config, scheduler, neighbor table, and routing instance.
     * @param fam   The AFI/SAFI this instance manages.
     */
    AddressFamilyInstance(config::BgpAddressFamilyRegistry& cfgs, BgpScope& scope, AfiSafi fam)
        : scope(scope),
          family(fam),
          policy(ScopeAccessor::getRoutingInstance(scope), scope),
          igpMetricResolver([](const types::IPAddress&) { return std::numeric_limits<uint64_t>::max(); }),
          configs(cfgs),
          egress(scope, fam)
    {
        configs.context().set(static_cast<AfInstanceBase*>(this));
        configs.get<config::BgpAddressFamily::AF_BASE>().get().context().set(static_cast<AfInstanceBase*>(this));
        syncNetworkRoutes();
    }

    AddressFamilyInstance(const AddressFamilyInstance&) = delete;
    AddressFamilyInstance& operator=(const AddressFamilyInstance&) = delete;
    AddressFamilyInstance(AddressFamilyInstance&&) = delete;
    AddressFamilyInstance& operator=(AddressFamilyInstance&&) = delete;

    /**
     * @brief Tears down all active timers and RIB watches before destruction.
     *
     * Cancels the NHT recompute timer and all per-peer stale-path / max-EOR
     * timers. Removes all network-command RIB watches and NHT watches. This
     * must complete before the BgpScope scheduler is destroyed, because
     * timer callbacks hold a pointer to this instance.
     */
    ~AddressFamilyInstance()
    {
        configs.context().reset();
        auto& sched = ScopeAccessor::getScheduler(scope);
        if (dirty.timerId != 0)
            sched.cancel(dirty.timerId);
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

    /**
     * @brief Installs a callback used to resolve IGP metrics for next-hop addresses.
     *
     * The resolver is called during best-path selection to compare routes by IGP
     * cost when all other attributes are equal (RFC 4271 §9.1.2.2, step 9).
     * Typically wired up by the BgpScope to query OSPF or EIGRP metrics.
     *
     * @param resolver  Callable mapping a next-hop IPAddress to its IGP cost.
     *                  Return UINT64_MAX to indicate the next-hop is unreachable.
     */
    void setIgpMetricResolver(IgpMetricResolver resolver)
    {
        igpMetricResolver = std::move(resolver);
    }

    /**
     * @brief Processes a raw inbound UPDATE message for this AFI.
     *
     * Delegates wire decoding to BgpRx::processUpdate, then hands the parsed
     * result to the internal route-processing pipeline. On parse error, `error`
     * is populated with a NOTIFICATION to send back to the peer.
     *
     * @param session  Session the UPDATE arrived on.
     * @param uinfo    Raw UPDATE payload from the TCP stream.
     * @param error    Output: NOTIFICATION descriptor to send if parsing fails.
     * @return True on success; false if a parse error occurred and `error` is set.
     */
    bool onUpdateFromPeer(Session& session, IncomingUpdate& uinfo, Notification& error)
    {
        ParsedUpdate<NlriT> update;

        if (!BgpRx::processUpdate<N>(session, uinfo, update, error))
            return false;

        onParsedUpdateFromPeer(session.neighbor, update);

        return true;
    }

    /**
     * @brief Sends the full Loc-RIB to a newly established peer and fires default-originate/aggregates.
     *
     * If Enhanced Route Refresh is negotiated, wraps the initial dump with BORR/EORR
     * messages (RFC 7313). Called once per session by BgpScope on FSM transition
     * to ESTABLISHED.
     *
     * @param session  Newly established peer session.
     */
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

    /**
     * @brief Re-sends the full Adj-RIB-Out to a peer in response to a ROUTE-REFRESH request.
     *
     * Groups outbound NLRIs by path attribute to minimise UPDATE message count.
     * Wraps the refresh with BORR/EORR when Enhanced Route Refresh is active.
     *
     * @param session  Peer session that sent the ROUTE-REFRESH.
     */
    void refreshPeer(Session& session)
    {
        const uint32_t peerRid = session.getPeerRid();
        const bool enhancedRR = session.getNegotiated().enhancedRR;

        if (enhancedRR)
            session.sendRouteRefresh(family, RouteRefreshReason::Borr);

        auto outIt = adjRibOut.find(peerRid);
        if (outIt != adjRibOut.end())
        {
            auto& attrMgr = ScopeAccessor::getAttrMgr(scope);

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

    /**
     * @brief Handles a BORR (Begin-of-Route-Refresh) from a peer (RFC 7313).
     *
     * Marks all current Adj-RIB-In entries from this peer as stale and starts
     * the STALEPATH and MAX-EOR timers. If EORR does not arrive before the
     * timers fire, stale entries are purged by purgeStalePeer.
     *
     * @param session  Peer session that sent the BORR.
     */
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

        auto& sched = ScopeAccessor::getScheduler(scope);
        auto& procCfgs = ScopeAccessor::getConfigs(scope);

        auto& sti = staleTimers[peerRid];

        uint16_t staleSecs = procCfgs.get<config::Bgp::BGP_REFRESH_STALEPATH_TIME>().load();
        sti.stalepath = sched.postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(staleSecs),
            [this, peerRid](uint32_t) { staleTimers[peerRid].stalepath = 0; purgeStalePeer(peerRid); });

        uint16_t maxEorSecs = procCfgs.get<config::Bgp::BGP_REFRESH_MAX_EOR_TIME>().load();
        sti.maxEor = sched.postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(maxEorSecs),
            [this, peerRid](uint32_t) { staleTimers[peerRid].maxEor = 0; purgeStalePeer(peerRid); });
    }

    /**
     * @brief Purges all remaining stale Adj-RIB-In entries when a peer sends EORR.
     *
     * Cancels the STALEPATH and MAX-EOR timers that were armed by @ref onPeerBorr,
     * then calls @ref purgeStalePeer to remove any NLRIs that the peer did not
     * re-advertise during the route-refresh cycle.
     *
     * @param session  Peer session that sent the EORR.
     *
     * @see onPeerBorr, purgeStalePeer
     */
    void onPeerEorr(Session& session)
    {
        const uint32_t peerRid = session.getPeerRid();
        cancelStaleTimers(peerRid);
        purgeStalePeer(peerRid);
    }

    /**
     * @brief Re-evaluates all active Loc-RIB entries on the BGP_SCAN_TIME periodic tick.
     *
     * Iterates every entry in the Loc-RIB and calls @ref recomputeAdjRibOut so that
     * any attribute or policy changes that occurred since the last scan are reflected
     * in outbound UPDATE messages to all established peers.
     */
    void scan()
    {
        for (auto& [nlri, route] : locRib)
            recomputeAdjRibOut(nlri, &route);
    }

    /**
     * @brief Re-applies the current inbound policy to all stored pre-policy routes from a peer.
     *
     * Clears the post-policy Adj-RIB-In for `peerRid`, then replays every entry in
     * the pre-policy table through @ref applyIngressPolicy and re-installs accepted
     * routes. Prefixes whose acceptance status changes are passed to @ref recomputeNlri
     * so that the Loc-RIB and Adj-RIB-Out are updated accordingly.
     *
     * Requires that soft-reconfiguration inbound was enabled for this neighbor when
     * the routes were first received; if the pre-policy table is empty, this is a no-op.
     *
     * @param peerRid  Router ID of the peer whose inbound policy is to be re-applied.
     */
    void softClearInbound(uint32_t peerRid)
    {
        auto preIt = preAdjRibIn.find(peerRid);
        if (preIt == preAdjRibIn.end()) return;

        const NeighborAf* nbrAf = ScopeAccessor::getNtable(scope).lookup(peerRid, family);
        if (!nbrAf) return;

        auto& attrMgr = ScopeAccessor::getAttrMgr(scope);

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
            InboundRoute<NlriT> r(attrMgr, pid, nlriPath.nlri, const_cast<NeighborAf*>(nbrAf));
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

    /**
     * @brief Tears down all per-peer state when a session goes down.
     *
     * Performs a full cleanup for `peer`:
     * - Cancels any pending MRAI timer and removes the MRAI state entry.
     * - Resets per-AF neighbor flags (ORF filter, max-prefix warning, slow-peer state).
     * - Cancels stale-path timers from any in-progress route-refresh cycle.
     * - Removes the pre-policy Adj-RIB-In and Adj-RIB-Out entries.
     * - Removes all post-policy Adj-RIB-In entries and evicts affected prefixes from
     *   the Loc-RIB, then calls @ref recomputeNlri for each touched NLRI so the
     *   remaining peers receive the correct withdraw or re-advertisement.
     *
     * @param peer  Router ID of the peer whose session went down.
     */
    void invalidatePeer(uint32_t peer)
    {
        auto mraiIt = mraiState.find(peer);
        if (mraiIt != mraiState.end())
        {
            if (mraiIt->second.timerId != 0)
                ScopeAccessor::getScheduler(scope).cancel(mraiIt->second.timerId);
            mraiState.erase(mraiIt);
        }

        if (NeighborAf* nbrAf = ScopeAccessor::getNtable(scope).lookup(peer, family); nbrAf)
        {
            nbrAf->invalidate();
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
            keys.insert(nlriPath.nlri);

        adjRibIn.erase(it);

        for (const auto& n : keys)
            recomputeNlri(n);
    }

    /**
     * @brief Re-ranks Adj-RIB-In candidates and rebuilds the ADD-PATH pool for every
     *        installed prefix in this AF, without rerunning best-path selection.
     *
     * Triggered when an advertise additional path config field changes: the
     * best-path winner for each prefix is untouched, so this skips @ref DecisionEngine::selectBest
     * and dampening, re-gathers and ranks candidates per prefix, rebuilds `additionalPaths`
     * via @ref buildAdditionalPathsPool, and re-advertises via @ref recomputeAdjRibOut.
     */
    void recomputeAdditionalPaths()
    {
        BestPathConfig bpCfg;
        bpCfg.compareMed        = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_ALWAYS_COMPARE_MED>().load();
        bpCfg.compareRouterId   = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_BEST_PATH_COMPARE_ROUTER_ID>().load();
        bpCfg.medMissingAsWorst = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_BEST_PATH_MED_MISSING_AS_WORST>().load();
        bpCfg.ignoreIgpMetric   = configs.get<config::BgpAddressFamily::BGP_BEST_PATH_IGP_METRIC_IGNORE>().load();
        bpCfg.medConfed         = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_BEST_PATH_MED_CONFED>().load();
        DecisionEngine decision(scope, bpCfg);

        for (auto& [nlri, best] : locRib)
        {
            std::vector<InboundRoute<NlriT>*> candidates;
            for (auto& [peer, peerTable] : adjRibIn)
            {
                for (auto& [key, route] : peerTable)
                {
                    if (key.nlri != nlri)
                        continue;
                    candidates.push_back(&route);
                }
            }

            {
                auto it = networkLocalRoutes.find(nlri);
                if (it != networkLocalRoutes.end())
                    candidates.push_back(&it->second);
            }

            buildAdditionalPathsPool(best, decision.rankCandidates(candidates));
            recomputeAdjRibOut(nlri, &best);
        }
    }

    void markInboundDirty(InDirty category, uint32_t peerRid = 0) override
    {
        dirty.markIn(category);
        dirty.markInPeer(peerRid);
        armFlush();
    }

    void markNeighborOutDirty(uint32_t peerRid) override
    {
        dirty.markOutNeighbor(peerRid);
        armFlush();
    }

    void markAfDirty(AfDirty category) override
    {
        dirty.markAf(category);
        armFlush();
    }

    /// Config-thread entry point: hops onto the BGP scheduler before marking.
    void enqueueMarkAfDirty(AfDirty category) override
    {
        ScopeAccessor::getScheduler(scope).post(
            [this, category]() { markAfDirty(category); });
    }

    void enqueueSyncNetwork() override
    {
        enqueueMarkAfDirty(AfDirty::NETWORK);
    }

private:
    void armFlush()
    {
        dirty.arm(ScopeAccessor::getScheduler(scope), [this]() { flushDirty(); });
    }

    /**
     * @brief Drains the accumulated dirty state and recomputes only the affected stages.
     *
     * Runs on the BGP scheduler thread when the debounce window elapses. The dirty state
     * is snapshotted and cleared up-front (@ref DirtyState::drain) so that marks posted
     * while this pass runs accumulate into a fresh state and arm their own next flush;
     * this is also the single seam a future background-thread offload would snapshot at.
     *
     * Stages execute in dependency order: network watches, then inbound re-evaluation,
     * then best-path / RIB, then ADD-PATH pool, then outbound re-advertisement.
     */
    void flushDirty()
    {
        DirtyState snap = dirty.drain();

        // 1. NETWORK: reconcile locally-originated network-command routes/watches.
        //    syncNetworkRoutes() itself calls recomputeNlri for any prefix it changes.
        if (snap.testAf(AfDirty::NETWORK))
            syncNetworkRoutes();

        // 2. Inbound: re-run ingress policy / acceptance for the affected peers.
        if (snap.in.any())
        {
            if (snap.inPeers.empty())
            {
                // Whole-AF inbound change: re-run every peer's inbound pipeline.
                std::vector<uint32_t> peers;
                peers.reserve(adjRibIn.size());
                for (const auto& [peerRid, _] : adjRibIn)
                    peers.push_back(peerRid);
                for (uint32_t peerRid : peers)
                    reevaluateInbound(peerRid);
            }
            else
            {
                for (uint32_t peerRid : snap.inPeers)
                    reevaluateInbound(peerRid);
            }
        }

        // 3. Best-path / Loc-RIB: rerun selection or reinstall over every prefix.
        const bool rerunBest = snap.testAf(AfDirty::BEST_PATH)
                            || snap.testAf(AfDirty::MAX_PATHS)
                            || snap.testAf(AfDirty::DAMPENING);
        if (rerunBest)
        {
            std::vector<NlriT> keys;
            keys.reserve(locRib.size());
            for (const auto& [nlri, _] : locRib)
                keys.push_back(nlri);
            // recomputeNlri handles both re-selection and downstream Adj-RIB-Out.
            recomputeNlri(keys);
        }
        else if (snap.testAf(AfDirty::DISTANCE))
        {
            // Distance-only: reinstall winners with the new admin distance/metric,
            // no best-path re-selection needed.
            std::vector<LocalRoute<NlriT>*> installs;
            installs.reserve(locRib.size());
            for (auto& [nlri, route] : locRib)
                installs.push_back(&route);
            if (!installs.empty())
                installToRib(installs);
        }

        // 4. AF-level ADD-PATH candidate pool.
        if (snap.testAf(AfDirty::ADD_PATH_SELECT))
            recomputeAdditionalPaths();

        // 5. Aggregates.
        if (snap.testAf(AfDirty::AGGREGATE))
            scheduleAggregateRecompute();

        // 6. Outbound: partial rebuild per dirty neighbor. Skipped when a best-path rerun
        //    already refreshed every Adj-RIB-Out entry above.
        if (!rerunBest)
        {
            for (uint32_t peerRid : snap.outNeighbors)
                flushOutboundNeighbor(peerRid);
        }
    }

    /**
     * @brief Partial outbound rebuild for one neighbor: re-derives only its dirty attributes.
     *
     * Reads and clears the neighbor's dirty-attribute mask, then walks its Adj-RIB-Out
     * grouped by the shared source attribute set (the stored egress pathId). For each
     * group it re-derives only the dirty attributes onto the current attributes, and if
     * the result changed, acquires a new flyweight pathId, repoints the group, and sends
     * one UPDATE covering all the group's prefixes. ADD_PATH / ACTIVATE bits fall back to
     * a full per-prefix recompute for the affected prefixes since they change path
     * membership rather than a single attribute.
     */
    void flushOutboundNeighbor(uint32_t peerRid)
    {
        NeighborAf* nbrAf = ScopeAccessor::getNtable(scope).lookup(peerRid, family);
        if (!nbrAf) return;

        Session* session = nbrAf->getSession();
        if (!session || !session->established()) return;
        if (!session->getNegotiated().activeFamilies.count(family)) return;

        OutAttrMask dirtyAttrs = nbrAf->drainDirtyOut();
        if (dirtyAttrs.none()) return;

        auto outIt = adjRibOut.find(peerRid);
        if (outIt == adjRibOut.end()) return;
        PerPeerOutTable<NlriT>& peerOut = outIt->second;

        // Membership / path-set changes: these are not single-attribute rewrites, so
        // recompute the affected prefixes in full.
        if (testBit(dirtyAttrs, OutAttr::ACTIVATE) || testBit(dirtyAttrs, OutAttr::ADD_PATH))
        {
            std::unordered_set<NlriT> prefixes;
            for (auto& [nlri, _] : peerOut)
                prefixes.insert(nlri);
            for (const NlriT& nlri : prefixes)
            {
                auto lit = locRib.find(nlri);
                recomputeAdjRibOut(nlri, lit != locRib.end() ? &lit->second : nullptr);
            }
            // Attribute-only bits still handled below over whatever remains.
            dirtyAttrs.reset(static_cast<size_t>(OutAttr::ACTIVATE));
            dirtyAttrs.reset(static_cast<size_t>(OutAttr::ADD_PATH));
            if (dirtyAttrs.none()) return;
        }

        auto& attrMgr = ScopeAccessor::getAttrMgr(scope);

        // Group entries by their current egress pathId (shared attribute set).
        std::map<std::pair<uint32_t, const InboundRoute<NlriT>*>,
                 std::vector<typename PerPeerOutTable<NlriT>::iterator>> groups;
        for (auto it = peerOut.begin(); it != peerOut.end(); ++it)
        {
            if (!it->second.second.pathId.has_value())
                continue;
            const InboundRoute<NlriT>* src = findEgressSource(it, peerRid);
            if (!src)
                continue;
            groups[{*it->second.second.pathId, src}].push_back(it);
        }

        for (auto& [key, entries] : groups)
        {
            const uint32_t oPid = key.first;
            const InboundRoute<NlriT>* srcRoute = key.second;

            PathAttribute src = srcRoute->getPathAttributes();
            PathAttribute pa  = attrMgr.get(oPid);
            reDeriveAttrs(pa, src, *srcRoute, *nbrAf, *session, dirtyAttrs);

            // acquire() yields one reference. Each Adj-RIB-Out entry's OutboundRoute takes
            // ownership of exactly one pre-counted reference, so hold K total: the acquire
            // ref covers the first entry, retain once per additional entry.
            uint32_t newPid = attrMgr.acquire(pa.attrs, pa.path);
            if (newPid == oPid)
            {
                attrMgr.release(newPid);
                continue;
            }

            BuildUpdate<NlriT> update;
            typename BuildUpdate<NlriT>::Announcement ann;
            ann.attrs = std::move(pa);
            bool firstEntry = true;
            for (auto it : entries)
            {
                if (!firstEntry)
                    attrMgr.retain(newPid);
                firstEntry = false;
                ann.nlri.push_back({it->first, it->second.first});
                it->second.second = OutboundRoute<NlriT>{attrMgr, newPid, it->first};
            }
            update.announcements.push_back(std::move(ann));
            session->sendUpdate<N>(update);
        }
    }

    /**
     * @brief Re-derives only the attributes set in @p dirtyAttrs onto @p pa from @p src.
     */
    void reDeriveAttrs(PathAttribute& pa, const PathAttribute& src, const InboundRoute<NlriT>& route,
                       const NeighborAf& nbrAf, const Session& session, const OutAttrMask& dirtyAttrs)
    {
        if (testBit(dirtyAttrs, OutAttr::NEXT_HOP))
            egress.deriveNextHop(pa, src, route, nbrAf, session);
        if (testBit(dirtyAttrs, OutAttr::AS_PATH))
            egress.deriveAsPath(pa, src, nbrAf, session);
        if (testBit(dirtyAttrs, OutAttr::COMMUNITIES))
            egress.deriveCommunities(pa, src, nbrAf, session);
        if (testBit(dirtyAttrs, OutAttr::EXT_COMMUNITIES))
            egress.deriveExtCommunities(pa, src, nbrAf, session);
        if (testBit(dirtyAttrs, OutAttr::LARGE_COMMUNITIES))
            egress.deriveLargeCommunities(pa, src, nbrAf, session);
        if (testBit(dirtyAttrs, OutAttr::LOCAL_PREF))
            egress.deriveLocalPref(pa, src, session);
        if (testBit(dirtyAttrs, OutAttr::REFLECTION))
        {
            const bool fromIbgp = !route.ebgp && !route.confedEbgp;
            const bool toIbgp   = !session.neighbor.isEbgp() && !session.neighbor.isConfedEbgp();
            if (fromIbgp && toIbgp && route.sourceNeighbor)
            {
                if (!pa.attrs.originatorId.has_value())
                    pa.attrs.originatorId = route.sourceNeighbor->getParent().getRouterId();
                pa.attrs.clusterList = src.attrs.clusterList;
                pa.attrs.clusterList.insert(pa.attrs.clusterList.begin(), getClusterId());
            }
        }
    }

    /**
     * @brief Finds the source InboundRoute for an Adj-RIB-Out entry (by NLRI and add-path id).
     */
    const InboundRoute<NlriT>* findEgressSource(typename PerPeerOutTable<NlriT>::iterator entry,
                                                uint32_t peerRid)
    {
        const NlriT& nlri = entry->first;
        uint32_t addPathId = entry->second.first;

        auto lit = locRib.find(nlri);
        if (lit == locRib.end()) return nullptr;
        LocalRoute<NlriT>& best = lit->second;

        if (addPathId == 0)
            return &best.route;

        // ADD-PATH: source is the path whose origin peer RID matches addPathId.
        auto matches = [&](const InboundRoute<NlriT>* r) {
            return r->sourceNeighbor && r->sourceNeighbor->getParent().getRouterId() == addPathId;
        };
        if (matches(&best.route)) return &best.route;
        for (auto* mp : best.multipaths)
            if (matches(mp)) return mp;
        for (auto* ap : best.additionalPaths)
            if (matches(ap)) return ap;
        return nullptr;
    }

    /**
     * @brief Re-runs the inbound pipeline for one peer after an inbound-policy config change.
     *
     * Prefers a soft-reconfiguration replay when a pre-policy Adj-RIB-In copy is retained;
     * otherwise re-runs best-path over the prefixes currently in the peer's Adj-RIB-In so
     * acceptance/limit changes take effect. A no-op if the peer has no Adj-RIB-In.
     *
     * @param peerRid  Router ID of the peer to re-evaluate.
     */
    void reevaluateInbound(uint32_t peerRid)
    {
        if (preAdjRibIn.count(peerRid))
        {
            softClearInbound(peerRid);
            return;
        }
        auto inIt = adjRibIn.find(peerRid);
        if (inIt == adjRibIn.end())
            return;
        std::vector<NlriT> keys;
        keys.reserve(inIt->second.size());
        for (const auto& [key, _] : inIt->second)
            keys.push_back(key.nlri);
        recomputeNlri(keys);
    }

    /**
     * @brief Core inbound route processing pipeline for a decoded UPDATE message.
     *
     * Processes all withdrawals and announcements in `update` for `peer`:
     * 1. Removes each withdrawn NLRI from the post-policy Adj-RIB-In, the pre-policy
     *    table (if soft-reconfiguration is enabled), and the stale set.
     * 2. For each announced NLRI, stores a pre-policy copy (if soft-reconfiguration is
     *    enabled), constructs an @ref InboundRoute, and runs it through
     *    @ref applyIngressPolicy. Accepted routes replace any existing entry in the
     *    post-policy Adj-RIB-In and are removed from the stale set.
     * 3. Enforces MAXIMUM_PREFIX: sends `MAX_PREFIX_REACHED` to the FSM and schedules
     *    a restart when the post-policy prefix count exceeds the configured limit.
     * 4. Calls @ref recomputeNlri for every touched prefix so the Loc-RIB and
     *    Adj-RIB-Out are updated.
     *
     * @param peer    Neighbor that sent the UPDATE (must be present in the neighbor table).
     * @param update  Decoded UPDATE payload from @ref BgpRx::processUpdate.
     */
    void onParsedUpdateFromPeer(Neighbor& peer, ParsedUpdate<NlriT>& update)
    {
        uint32_t rid = peer.getRouterId();
        NeighborAf* nbrAf = ScopeAccessor::getNtable(scope).lookup(rid, family);
        if (!nbrAf) return;

        PerPeerInTable<NlriT>& peerIn = adjRibIn[rid];

        auto& nbrAfCfgs = nbrAf->configs;
        bool softReconfig = nbrAfCfgs.get<config::BgpNeighbor::SOFT_RECONFIGURATION>().load()
                         || configs.get<config::BgpAddressFamily::BGP_SOFT_RECONFIG_BACKUP>().load();

        std::unordered_set<NlriT> touched;

        // Withdrawn: n is NlriPath<NlriT>
        for (const auto& n : update.withdrawn)
        {
            auto it = peerIn.find(n);
            if (it != peerIn.end())
            {
                peerIn.erase(it);
            }
            if (softReconfig)
            {
                auto preIt = preAdjRibIn.find(rid);
                if (preIt != preAdjRibIn.end())
                    preIt->second.erase(n);
            }
            // Peer withdrew this NLRI explicitly; remove from stale set.
            {
                auto stIt = stalePeerNlris.find(rid);
                if (stIt != stalePeerNlris.end())
                    stIt->second.erase(n.nlri);
            }
            touched.insert(n.nlri);
        }

        if (update.attrs.has_value())
        {
            auto& attrMgr = ScopeAccessor::getAttrMgr(scope);

            const uint32_t peerAs = nbrAf->getRemoteAs().value_or(0);
            const bool isEbgp     = nbrAf->isEbgp();
            const bool isConfed   = nbrAf->isConfedEbgp();

            uint32_t pid = attrMgr.acquire(update.attrs->attrs, update.attrs->path);

            // Store pre-policy copy of all announced NLRIs for soft-reconfiguration.
            if (softReconfig)
            {
                auto& preIn = preAdjRibIn[rid];
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
                InboundRoute<NlriT> r(attrMgr, pid, n.nlri, nbrAf);
                r.sourceNeighbor   = nbrAf;
                r.peerAs           = peerAs;
                r.ebgp             = isEbgp;
                r.confedEbgp       = isConfed;
                r.igpCost           = resolveIgpMetric(update.attrs->path.nextHop);

                auto weight = nbrAf->configs.get<config::BgpNeighbor::WEIGHT>();
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
                    auto stIt = stalePeerNlris.find(rid);
                    if (stIt != stalePeerNlris.end())
                        stIt->second.erase(n.nlri);
                }
                touched.insert(n.nlri);
            }
        }

        // MAXIMUM_PREFIX enforcement
        {
            auto maxPfxField = nbrAfCfgs.get<config::BgpNeighbor::MAXIMUM_PREFIX>();
            if (maxPfxField.hasValue())
            {
                uint32_t maxPfx = maxPfxField.load();
                uint32_t count = static_cast<uint32_t>(peerIn.size());
                bool warningOnly = nbrAfCfgs.get<config::BgpNeighbor::MAXIMUM_PREFIX_WARNING_ONLY>().load();

                // Threshold warning: fire once per session when count reaches N% of limit.
                auto threshField = nbrAfCfgs.get<config::BgpNeighbor::MAXIMUM_PREFIX_THRESHOLD>();
                uint8_t threshold = threshField.hasValue() ? threshField.load() : 75;
                if (!nbrAf->maxPfxWarned && count >= maxPfx * threshold / 100)
                {
                    nbrAf->maxPfxWarned = true;
                    // TODO: log warning
                }

                if (auto* sess = nbrAf->getSession(); sess && count >= maxPfx && !warningOnly)
                {
                    auto restartField = nbrAfCfgs.get<config::BgpNeighbor::MAXIMUM_PREFIX_RESTART>();
                    if (restartField.hasValue())
                        nbrAf->schedulePfxRestart(restartField.load());
                    sess->postEvent(FsmEvent::MAX_PREFIX_REACHED);
                }
            }
        }

        for (const NlriT& nlri : touched)
            recomputeNlri(nlri);
    }

    /**
     * @brief Runs best-path selection and updates the Loc-RIB for a single prefix.
     *
     * Convenience wrapper that forwards to the batch overload with a one-element vector.
     *
     * @param nlri  Prefix to recompute.
     */
    void recomputeNlri(const NlriT& nlri)
    {
        recomputeNlri(std::vector<NlriT>{nlri});
    }

    /**
     * @brief Runs best-path selection and updates the Loc-RIB for a set of prefixes.
     *
     * For each NLRI in `nlris`:
     * - Collects all candidate @ref InboundRoute entries from every peer's Adj-RIB-In,
     *   refreshing IGP costs, plus any locally-originated network-command routes.
     * - Applies deterministic-MED grouping when BGP_DETERMINISTIC_MED is configured.
     * - Passes candidates to @ref DecisionEngine::selectBest (and ADD-PATH pool logic).
     * - Installs the winning route into the Loc-RIB via @ref installToRib and schedules
     *   aggregate recompute; withdraws and removes the entry if no best exists.
     * - Drives @ref recomputeAdjRibOut so all peers receive the correct announcement or
     *   withdrawal.
     *
     * @param nlris  Prefixes to recompute.
     */
    void recomputeNlri(const std::vector<NlriT> nlris)
    {
        std::vector<LocalRoute<NlriT>*> installs;
        installs.reserve(nlris.size());

        BestPathConfig bpCfg;
        bpCfg.compareMed        = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_ALWAYS_COMPARE_MED>().load();
        bpCfg.compareRouterId   = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_BEST_PATH_COMPARE_ROUTER_ID>().load();
        bpCfg.medMissingAsWorst = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_BEST_PATH_MED_MISSING_AS_WORST>().load();
        bpCfg.ignoreIgpMetric   = configs.get<config::BgpAddressFamily::BGP_BEST_PATH_IGP_METRIC_IGNORE>().load();
        bpCfg.medConfed         = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_BEST_PATH_MED_CONFED>().load();

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

            if (ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_DETERMINISTIC_MED>().load())
            {
                static const types::IPAddress kDetEmpty{};
                auto detNbr = [](const InboundRoute<NlriT>* r) -> const types::IPAddress& {
                    return r->sourceNeighbor ? r->sourceNeighbor->getParent().neighborAddress : kDetEmpty;
                };
                BestPathComparator detCmp(scope, bpCfg);

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

            DecisionEngine decision(scope, bpCfg);
            std::optional<LocalRoute<NlriT>> best = decision.selectBest(
                candidates,
                configs.get<config::BgpAddressFamily::MAXIMUM_PATHS_EBGP>().load(),
                configs.get<config::BgpAddressFamily::MAXIMUM_PATHS_IBGP>().load()
            );

            // Build ADD-PATH candidate pool for additional-paths advertisement.
            if (best.has_value())
                buildAdditionalPathsPool(*best, decision.rankCandidates(candidates));

            auto lit = locRib.find(nlri);
            const bool had = (lit != locRib.end());

            if (!best.has_value())
            {
                if (had)
                {
                    withdrawFromRib(nlri);
                    locRib.erase(lit);
                    recomputeAdjRibOut(nlri, nullptr);
                    if constexpr (types::IsIPPrefix<NlriT>)
                        scheduleAggregateRecompute();
                }
                continue;
            }

            const bool bestChanged = !had || &lit->second.route != &best->route;

            // Always refresh the locRib entry so multipaths/additionalPaths stay current.
            if (had)
                locRib.erase(lit);
            locRib.emplace(nlri, *best);

            if (bestChanged)
                installs.push_back(&locRib.at(nlri));

            recomputeAdjRibOut(nlri, &locRib.at(nlri));
            if constexpr (types::IsIPPrefix<NlriT>)
                scheduleAggregateRecompute();
        }

        installToRib(installs);
    }

    /**
     * @brief Rebuilds `best.additionalPaths` from a pre-ranked candidate list per the
     *        AF's ADD-PATH selection config (ALL / BEST-n / BACKUP / BEST-EXTERNAL / GROUP-BEST).
     *
     * Does not touch `best.route` or `best.multipaths` -- the best-path winner itself is
     * unaffected by these fields. Callers that already have a ranked candidate list from
     * a fresh @ref DecisionEngine::selectBest run (e.g. @ref recomputeNlri) may pass it
     * directly; @ref recomputeAdditionalPaths re-ranks the already-installed candidates
     * for a config-only change that doesn't warrant a full best-path rerun.
     *
     * @param best        Loc-RIB entry whose `additionalPaths` pool is rebuilt in place.
     * @param rankedCandidates  Candidates for this NLRI, already ranked via
     *                          @ref DecisionEngine::rankCandidates.
     */
    void buildAdditionalPathsPool(LocalRoute<NlriT>& best, std::vector<InboundRoute<NlriT>*> rankedCandidates)
    {
        config::BgpAfBaseRegistry& base = configs.get<config::BgpAddressFamily::AF_BASE>().get();
        bool selectBackup    = base.get<config::BgpAfBase::ADDITIONAL_PATHS_SELECT_BACKUP>().load();
        bool selectBestExt   = base.get<config::BgpAfBase::ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL>().load();
        bool selectAll       = base.get<config::BgpAfBase::ADDITIONAL_PATHS_SELECT_ALL>().load();
        auto selectBestFld  = base.get<config::BgpAfBase::ADDITIONAL_PATHS_SELECT_BEST>();
        bool selectGroupBest = base.get<config::BgpAfBase::ADDITIONAL_PATHS_SELECT_GROUP_BEST>().load();

        best.additionalPaths.clear();

        if (!(selectAll || selectBackup || selectBestFld.hasValue() || selectBestExt || selectGroupBest))
            return;

        std::vector<InboundRoute<NlriT>*> pool;
        for (auto* r : rankedCandidates)
        {
            if (r == &best.route) continue;
            if (std::find(best.multipaths.begin(), best.multipaths.end(), r) != best.multipaths.end()) continue;
            pool.push_back(r);
        }

        if (selectAll)
        {
            best.additionalPaths = pool;
            return;
        }

        auto tryAdd = [&](InboundRoute<NlriT>* r) {
            if (std::find(best.additionalPaths.begin(), best.additionalPaths.end(), r) == best.additionalPaths.end())
                best.additionalPaths.push_back(r);
        };

        if (selectBestFld.hasValue())
        {
            uint8_t n = selectBestFld.load();
            for (auto* r : pool) {
                if (best.additionalPaths.size() >= n) break;
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
            seenAs.insert(best.route.peerAs);
            for (auto* mp : best.multipaths)
                seenAs.insert(mp->peerAs);
            for (auto* r : pool)
                if (seenAs.insert(r->peerAs).second)
                    tryAdd(r);
        }
    }

    /**
     * @brief Calls the NLRI policy install hook and registers a next-hop tracking watch for one route.
     *
     * Builds the install descriptor via @ref buildInstall, forwards it to the policy's
     * `installRoute` method, then calls @ref registerNht to watch the route's next-hop
     * address in the RIB.
     *
     * @param route  Best-path Loc-RIB entry to install.
     */
    void installToRib(LocalRoute<NlriT>& route)
    {
        auto install = buildInstall(route);
        policy.installRoute(install);
        registerNht(route.route.nlri, install.attrs.path.nextHop);
    }

    /**
     * @brief Calls the NLRI policy batch install hook and registers NHT watches for multiple routes.
     *
     * Builds install descriptors for all routes in `routes`, forwards the batch to the
     * policy's `installRoutes` method, then calls @ref registerNht for each installed
     * next-hop.
     *
     * @param routes  Pointers to Loc-RIB entries to install.
     */
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

    /**
     * @brief Calls the NLRI policy withdraw hook and removes the NHT watch for one prefix.
     *
     * Unregisters the next-hop tracking watch via @ref unregisterNht, then calls
     * the policy's `withdrawRoute` to remove the route from the VRF RIB.
     *
     * @param nlri  Prefix to withdraw.
     */
    void withdrawFromRib(const NlriT& nlri)
    {
        unregisterNht(nlri);
        policy.withdrawRoute(nlri);
    }

    /**
     * @brief Calls the NLRI policy batch withdraw hook and removes NHT watches for multiple prefixes.
     *
     * Unregisters the next-hop tracking watch for each NLRI, then forwards the full
     * list to the policy's `withdrawRoutes` method for a single batch operation.
     *
     * @param nlri  Prefixes to withdraw.
     */
    void withdrawFromRib(const std::vector<NlriT>& nlri)
    {
        for (const auto& n : nlri)
            unregisterNht(n);
        policy.withdrawRoutes(nlri);
    }

    /**
     * @brief Applies all inbound policy checks to a candidate route.
     *
     * Evaluates, in order:
     * - Route Reflector loop prevention (ORIGINATOR_ID and CLUSTER_LIST, iBGP only).
     * - AS-PATH loop detection: rejects routes containing the local AS more times than
     *   ALLOWAS_IN permits, and routes containing the configured LOCAL_AS.
     * - Confederation identifier loop detection.
     * - BGP_ENFORCE_FIRST_AS: rejects eBGP routes whose first AS does not match the
     *   neighbor's configured remote AS.
     * - BGP_MAX_AS_LIMIT: rejects routes whose AS-PATH length exceeds the limit.
     * - BGP_MAX_COMMUNITY_LIMIT / BGP_MAX_EXT_COMMUNITY_LIMIT: rejects routes carrying
     *   too many community attributes.
     *
     * @param route  Candidate inbound route to evaluate.
     * @return `true` if the route must be dropped; `false` to accept into Adj-RIB-In.
     *
     * @note Route-map / prefix-list / community-filter ingress policy is not yet
     *       implemented (marked TODO below); only the hard limit checks above apply.
     */
    bool applyIngressPolicy(const InboundRoute<NlriT>& route)
    {
        if (!route.sourceNeighbor)
            return false;

        PathAttribute pathAttrs = ScopeAccessor::getAttrMgr(scope).get(*route.pathId);
        uint32_t routerAs = ScopeAccessor::getAsNum(scope);
        auto& nbr = route.sourceNeighbor->getParent();

        // Route Reflector loop prevention (RFC 4456 §8): only for iBGP (not confed-eBGP).
        if (!route.ebgp && !route.confedEbgp)
        {
            const uint32_t ourRid       = ScopeAccessor::getRid(scope);
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
            const NeighborConfigs& nbrCfgs = nbr.getConfigs();
            auto localAsField = nbrCfgs.get<config::BgpNeighborSession::LOCAL_AS>();
            bool localAsEnabled = localAsField.hasValue();
            auto localProps = localAsField.hasValue() ? localAsField.load().props() : 0;
            bool dualAs = localProps.test(config::bgp::BgpLocalAsProps::DUAL_AS);

            const NeighborAfConfigs& nbrAfCfgs = route.sourceNeighbor->configs;
            bool allowAsIn = nbrAfCfgs.get<config::BgpNeighbor::ALLOWAS_IN>().load();
            uint8_t maxOccurrences = 1;
            if (allowAsIn)
            {
                auto occField = nbrAfCfgs.get<config::BgpNeighbor::ALLOWAS_IN_OCCURANCES>();
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
                    if (localAsEnabled && !dualAs && localAsField.hasValue() && asn == localAsField.load().as())
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
            ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_ENFORCE_FIRST_AS>().load())
        {
            uint32_t fa = pathAttrs.attrs.firstAs();
            if (fa != 0 && fa != route.peerAs)
                return true; // reject: first-AS mismatch
        }

        // Max AS-PATH length: drop routes with an AS_PATH longer than the configured limit.
        {
            auto maxAsField = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_MAX_AS_LIMIT>();
            if (maxAsField.hasValue() && pathAttrs.attrs.asPathLength() > maxAsField.load())
                return true;
        }

        // Max community count: drop routes that carry too many standard communities.
        {
            auto maxComField = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_MAX_COMMUNITY_LIMIT>();
            if (maxComField.hasValue() && pathAttrs.attrs.communities.size() > maxComField.load())
                return true;
        }

        // Max extended community count.
        {
            auto maxExtField = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_MAX_EXT_COMMUNITY_LIMIT>();
            if (maxExtField.hasValue() && pathAttrs.attrs.extendedCommunities.size() > maxExtField.load())
                return true;
        }

        // TODO: ingress policy (route-maps, prefix-lists, community filters, etc.)
        return false; // accept
    }

    /**
     * @brief Per-peer Minimum Route Advertisement Interval (MRAI) rate-limit state.
     *
     * Tracks the timestamp of the last UPDATE sent to this peer and the set of NLRIs
     * whose advertisement was deferred because the interval had not yet elapsed. A
     * one-shot timer (identified by `timerId`) fires after the interval expires and
     * drains the pending set via @ref drainMraiPending.
     */
    struct MraiState
    {
        /// Last advertisement time per prefix; MRAI is scoped per (peer, prefix).
        std::unordered_map<NlriT, std::chrono::steady_clock::time_point> lastSentPerNlri;
        std::unordered_set<NlriT> pending;
        uint32_t timerId = 0;
    };

    /**
     * @brief MRAI timer callback: flushes deferred NLRIs for one peer.
     *
     * Sets `mraiBypassPeer` so that @ref recomputeAdjRibOut skips the rate-limit check
     * for this peer, then recomputes each pending NLRI. Clears `mraiBypassPeer` once
     * draining is complete so normal MRAI enforcement resumes.
     *
     * @param peerRid  Router ID of the peer whose MRAI timer fired.
     */
    void drainMraiPending(uint32_t peerRid)
    {
        auto it = mraiState.find(peerRid);
        if (it == mraiState.end()) return;

        std::unordered_set<NlriT> pending = std::move(it->second.pending);
        it->second.pending.clear(); // moved-from set is unspecified; make it definitively empty
        it->second.timerId = 0;

        mraiBypassPeer = peerRid;
        for (const NlriT& nlri : pending)
        {
            auto lit = locRib.find(nlri);
            recomputeAdjRibOut(nlri, lit != locRib.end() ? &lit->second : nullptr);
        }
        mraiBypassPeer = 0;
    }

    /**
     * @brief Recomputes and sends the Adj-RIB-Out entry for one NLRI to all established peers.
     *
     * Iterates every established neighbor and, for each:
     * - Withdraws the NLRI from the peer if `best` is null, or if any egress check fails
     *   (inactive next-hop via NHT, MRAI / slow-peer deferral, AF not negotiated, iBGP
     *   split-horizon, ACTIVATE not set, summary-only suppression, or ORF filter).
     * - Applies @ref applyGroupEgressPolicy and @ref applyMemberNexthop (or
     *   @ref applyEgressPolicy for ungrouped peers) and stamps Route Reflector attributes.
     * - Skips sending when the computed path attributes are identical to what was last
     *   sent, to avoid redundant UPDATEs.
     * - Batches ADD-PATH paths into a single UPDATE and removes stale ADD-PATH entries
     *   whose source routes are no longer in the candidate set.
     * - Updates slow-peer detection state from the TX backlog after each send.
     *
     * @param nlri  Prefix to recompute.
     * @param best  Winning Loc-RIB entry, or `nullptr` to withdraw the prefix from all peers.
     */
    void recomputeAdjRibOut(const NlriT& nlri, LocalRoute<NlriT>* best)
    {
        auto& attrMgr = ScopeAccessor::getAttrMgr(scope);

        ScopeAccessor::getNtable(scope).forEachSession([&](Session& session) {
            if (!session.established())
                return;

            const uint32_t peerRid = session.getPeerRid();
            NeighborAf* nbrAfPtr = session.neighbor.findAfNeighbor(family);
            if (!nbrAfPtr)
                return; // peer has no state for this AF; nothing to advertise
            PerPeerOutTable<NlriT>& peerOut = adjRibOut[peerRid];
            NeighborAf& nbrAf = *nbrAfPtr;
            auto& nbrAfCfgs = nbrAf.configs;

            auto withdrawFromPeer = [&]() {
                if (auto msIt = mraiState.find(peerRid); msIt != mraiState.end())
                {
                    msIt->second.lastSentPerNlri.erase(nlri);
                    msIt->second.pending.erase(nlri);
                }

                auto [begin, end] = peerOut.equal_range(nlri);
                if (begin == end) return;
                BuildUpdate<NlriT> withdraw;
                for (auto it = begin; it != end; ++it)
                    withdraw.withdrawn.push_back({nlri, it->second.first});
                peerOut.erase(begin, end);
                session.sendUpdate<N>(withdraw);
            };

            if (!best)
            {
                withdrawFromPeer();
                return;
            }

            // BGP_SUPPRESS_INACTIVE: suppress routes whose next-hop is not reachable via NHT.
            if (ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_SUPPRESS_INACTIVE>().load())
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

            if (peerRid != mraiBypassPeer)
            {
                uint16_t mraiSecs = nbrAfCfgs.get<config::BgpNeighbor::ADVERTISE_INTERVAL>().load();
                if (mraiSecs == kDefaultEbgpMraiSecs && !session.neighbor.isEbgp())
                    mraiSecs = kDefaultIbgpMraiSecs;

                if (mraiSecs > 0)
                {
                    auto& ms = mraiState[peerRid];
                    auto now = std::chrono::steady_clock::now();

                    auto sentIt = ms.lastSentPerNlri.find(nlri);
                    if (sentIt != ms.lastSentPerNlri.end())
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            now - sentIt->second);
                        if (elapsed.count() < mraiSecs)
                        {
                            ms.pending.insert(nlri);
                            if (ms.timerId == 0)
                            {
                                auto expiry = sentIt->second + std::chrono::seconds(mraiSecs);
                                ms.timerId = ScopeAccessor::getScheduler(scope).postAfter(expiry,
                                    [this, peerRid](uint32_t) { drainMraiPending(peerRid); });
                            }
                            return;
                        }
                    }
                }

                // Slow peer: defer if STATIC mode or TX buffer is backed up past detection threshold.
                {
                    auto slowMode = nbrAfCfgs.get<config::BgpAfBase::SLOW_PEER_MODE>();
                    bool isStatic = slowMode.hasValue() && slowMode.load() == config::bgp::SlowPeerMode::STATIC;

                    bool backlogged = false;
                    if (!isStatic && nbrAf.isSlowPeer)
                    {
                        auto* conn = session.getPrimaryConnection();
                        backlogged = conn && conn->pendingTxBytes() > 0;
                        if (!backlogged)
                        {
                            // Recover unless DYNAMIC_PERMANENT (check config live).
                            bool permanent = slowMode.load() == config::bgp::SlowPeerMode::DYNAMIC_PERMANENT;
                            if (!permanent)
                            {
                                nbrAf.isSlowPeer = false;
                                nbrAf.slowFirstSeen = {};
                            }
                        }
                    }

                    if (isStatic || backlogged)
                    {
                        auto& ms = mraiState[peerRid];
                        ms.pending.insert(nlri);
                        if (ms.timerId == 0)
                        {
                            uint16_t interval = nbrAfCfgs.get<config::BgpAfBase::SLOW_PEER_DETECTION_THRESHOLD>().load();
                            ms.timerId = ScopeAccessor::getScheduler(scope).postAfter(
                                std::chrono::steady_clock::now() + std::chrono::seconds(interval),
                                [this, peerRid](uint32_t) { drainMraiPending(peerRid); });
                        }
                        return;
                    }
                }
            }

            // Check that this AFI/SAFI was negotiated with this peer.
            const auto& negotiated = session.getNegotiated();
            if (!negotiated.activeFamilies.count(family))
            {
                withdrawFromPeer();
                return;
            }

            const bool fromIbgp = !best->route.ebgp && !best->route.confedEbgp;
            const bool toIbgp   = !session.neighbor.isEbgp() && !session.neighbor.isConfedEbgp();
            bool isReflecting = false;
            if (fromIbgp && toIbgp)
            {
                const bool locallyOriginated = (best->route.sourceNeighbor == nullptr);
                if (!locallyOriginated)
                {
                    const bool targetIsClient = nbrAfCfgs
                        .get<config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>().load();
                    const bool senderIsClient = best->route.sourceNeighbor->configs
                        .template get<config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>().load();

                    // Standard iBGP split-horizon: non-client to non-client.
                    if (!senderIsClient && !targetIsClient)
                    {
                        withdrawFromPeer();
                        return;
                    }

                    // Never reflect back to the originating client.
                    if (&best->route.sourceNeighbor->getParent() == &session.neighbor)
                    {
                        withdrawFromPeer();
                        return;
                    }

                    // Client-to-client reflection: honour the global toggle.
                    if (senderIsClient && targetIsClient &&
                        !ScopeAccessor::getConfigs(scope)
                            .get<config::Bgp::BGP_CLIENT_TO_CLIENT_REFLECTION>().load())
                    {
                        withdrawFromPeer();
                        return;
                    }

                    isReflecting = true;
                }
            }

            // ACTIVATE: only exchange routes when this AF is explicitly activated for the neighbor.
            if (!nbrAfCfgs.get<config::BgpNeighbor::ACTIVATE>().load())
            {
                withdrawFromPeer();
                return;
            }

            // Summary-only: suppress more-specifics covered by an active aggregate.
            if constexpr (types::IsIPPrefix<NlriT>)
            {
                if (aggregateSuppressed(nlri))
                {
                    withdrawFromPeer();
                    return;
                }
            }

            // ORF: apply peer-specified prefix-list filter on our outbound.
            if (!nbrAf.orfFilter.empty() && !egress.passesOrfFilter(nlri, nbrAf.orfFilter))
            {
                withdrawFromPeer();
                return;
            }

            const PeerGroup* pg = nbrAfCfgs.getPeerGroup();
            const bool addPathSend = negotiated.addPathSend(family);

            // Collect all paths to advertise based on per-neighbor ADVERTISE configs.
            std::vector<InboundRoute<NlriT>*> paths;
            paths.push_back(&best->route);

            if (addPathSend)
            {
                if (nbrAfCfgs.get<config::BgpNeighbor::ADVERTISE_DIVERSE_PATH_MPATH>().load())
                    for (auto* mp : best->multipaths)
                        paths.push_back(mp);

                if (!best->additionalPaths.empty())
                {
                    config::BgpAfBaseRegistry& baseCfg = configs.get<config::BgpAddressFamily::AF_BASE>().get();
                    bool backup    = baseCfg.get<config::BgpAfBase::ADDITIONAL_PATHS_SELECT_BACKUP>().load();
                    bool bestExt   = baseCfg.get<config::BgpAfBase::ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL>().load();
                    bool all       = baseCfg.get<config::BgpAfBase::ADDITIONAL_PATHS_SELECT_ALL>().load();
                    auto bestFld   = baseCfg.get<config::BgpAfBase::ADDITIONAL_PATHS_SELECT_BEST>();
                    bool groupBest = baseCfg.get<config::BgpAfBase::ADDITIONAL_PATHS_SELECT_GROUP_BEST>().load();

                    if (all)
                    {
                        for (auto* r : best->additionalPaths)
                            paths.push_back(r);
                    }
                    else if (bestFld.hasValue() || groupBest || bestExt || backup)
                    {
                        const size_t base = paths.size();
                        auto tryAdd = [&](InboundRoute<NlriT>* r) {
                            if (std::find(paths.begin() + base, paths.end(), r) == paths.end())
                                paths.push_back(r);
                        };

                        if (bestFld.hasValue())
                        {
                            uint8_t n = bestFld.load();
                            for (auto* r : best->additionalPaths) {
                                if (paths.size() - base >= n) break;
                                tryAdd(r);
                            }
                        }

                        if (backup && !best->additionalPaths.empty())
                            tryAdd(best->additionalPaths[0]);

                        if (bestExt)
                            for (auto* r : best->additionalPaths)
                                if (r->ebgp) { tryAdd(r); break; }

                        if (groupBest)
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
                        if (r->sourceNeighbor && r->sourceNeighbor->getParent().getRouterId() == apid)
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
                    ? route->sourceNeighbor->getParent().getRouterId() : 0;

                std::optional<PathAttribute> egressAttrs;
                if (pg)
                {
                    auto groupAttrs = egress.applyGroupEgressPolicy(*route, session);
                    if (!groupAttrs.has_value())
                        continue;
                    egress.applyMemberNexthop(*groupAttrs, *route, nbrAf, session);
                    egressAttrs = std::move(groupAttrs);
                }
                else
                {
                    egressAttrs = egress.applyEgressPolicy(*route, session);
                    if (!egressAttrs.has_value())
                        continue;
                }

                // Route Reflector (RFC 4456 §8): stamp ORIGINATOR_ID and prepend CLUSTER_LIST.
                if (isReflecting && route->sourceNeighbor)
                {
                    if (!egressAttrs->attrs.originatorId.has_value())
                        egressAttrs->attrs.originatorId = route->sourceNeighbor->getParent().getRouterId();
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
                session.sendUpdate<N>(update);

            if (!update.announcements.empty())
            {
                auto& ms = mraiState[peerRid];
                ms.lastSentPerNlri[nlri] = std::chrono::steady_clock::now();
                ms.pending.erase(nlri);
            }

            // Slow peer detection (DYNAMIC / DYNAMIC_PERMANENT): update state from TX backlog.
            if (!nbrAf.isSlowPeer)
            {
                bool detectEnabled = nbrAfCfgs.get<config::BgpAfBase::SLOW_PEER_DETECTION>().load();
                if (detectEnabled)
                {
                    auto* conn = session.getPrimaryConnection();
                    auto now = std::chrono::steady_clock::now();
                    if (conn && conn->pendingTxBytes() > 0)
                    {
                        if (nbrAf.slowFirstSeen == std::chrono::steady_clock::time_point{})
                            nbrAf.slowFirstSeen = now;

                        uint16_t thresh = nbrAfCfgs.get<config::BgpAfBase::SLOW_PEER_DETECTION_THRESHOLD>().load();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - nbrAf.slowFirstSeen);
                        if (elapsed.count() >= thresh)
                            nbrAf.isSlowPeer = true;
                    }
                    else
                    {
                        nbrAf.slowFirstSeen = {};
                    }
                }
            }
        });
    }

    /**
     * @brief Invokes the IGP metric resolver for a next-hop address.
     *
     * Returns `UINT64_MAX` when no resolver is installed (next-hop treated as
     * unreachable) or when the resolver itself indicates the next-hop is not reachable
     * via any IGP.
     *
     * @param nextHop  Next-hop address to resolve.
     * @return IGP cost to reach `nextHop`, or `UINT64_MAX` if unreachable.
     */
    uint64_t resolveIgpMetric(const types::IPAddress& nextHop) const
    {
        if (!igpMetricResolver)
            return std::numeric_limits<uint64_t>::max();
        return igpMetricResolver(nextHop);
    }

    /**
     * @brief Returns the configured cluster ID, falling back to the BGP router ID.
     *
     * Used by Route Reflector egress policy to stamp CLUSTER_LIST and by inbound loop
     * detection to reject routes carrying our own cluster ID.
     *
     * @return Configured BGP_CLUSTER_ID, or the scope's router ID if not set.
     */
    uint32_t getClusterId() const
    {
        return egress.getClusterId();
    }

    /**
     * @brief Returns the configured confederation identifier, falling back to the local AS.
     *
     * Used throughout egress policy and AS-PATH loop detection to identify the
     * confederation's public AS number. When no confederation is configured, the
     * local AS number is returned so the logic degenerates correctly.
     *
     * @return Configured BGP_CONFEDERATION_IDENTIFIER, or the local AS number if not set.
     */
    uint32_t getConfedId() const
    {
        return egress.getConfedId();
    }

    /**
     * @brief Per-aggregate-prefix origination state.
     *
     * Tracks whether an aggregate is currently being advertised (`active`) and
     * stores the pre-egress-policy PathAttribute (`basePa`) assembled by
     * @ref recomputeAggregate. Each established peer receives a per-session copy of
     * `basePa` after @ref applyAggregateEgressPolicy has been applied.
     */
    struct AggregateState
    {
        bool active = false;
        PathAttribute basePa;
    };

public:
    /**
     * @brief Originates the default route (0.0.0.0/0 or ::/0) to a specific peer.
     *
     * Sends a synthesised UPDATE announcing the AFI-appropriate default prefix.
     * The generated path attributes are built locally — no Loc-RIB entry is required.
     * AS-PATH and NEXT_HOP are adjusted for eBGP, confederation-eBGP, and iBGP
     * sessions using the same local-AS and confederation logic as regular egress policy.
     *
     * A no-op if DEFAULT_ORIGINATE is not enabled for this neighbor or the AFI is not
     * an IP prefix type. Records the peer RID in @ref defaultOriginatedPeers so that
     * @ref withdrawDefaultOriginate can issue the matching withdrawal.
     *
     * @param session  Peer session to send the default route to.
     *
     * @see withdrawDefaultOriginate
     */
    void sendDefaultOriginate(Session& session)
    {
        if constexpr (!types::IsIPPrefix<NlriT>)
            return;

        NeighborAf* nbrAfPtr = session.neighbor.findAfNeighbor(family);
        if (!nbrAfPtr)
            return;
        NeighborAf& nbrAf = *nbrAfPtr;
        if (!nbrAf.configs.get<config::BgpNeighbor::ACTIVATE>().load())
            return;
        if (!nbrAf.configs.get<config::BgpAfBase::DEFAULT_ORIGINATE>().load())
            return;

        NlriT defaultNlri{};
        if constexpr (requires { defaultNlri.af; })
        {
            if constexpr (N::afi.afi == BGP_AFI_IPV6)
                defaultNlri.af = types::AddressFamily::IPv6;
            else
                defaultNlri.af = types::AddressFamily::IPv4;
        }

        const bool isEbgp     = session.neighbor.isEbgp();
        const bool isConfedEbgp = session.neighbor.isConfedEbgp();
        const uint32_t routerAs = ScopeAccessor::getAsNum(scope);
        auto& sesCfgs = session.getNeighborConfigs();

        PathAttribute pa{};
        pa.attrs.origin = BGP_ORIGIN_IGP;

        if (isEbgp)
        {
            AsPathSegment seg;
            seg.segmentType = BGP_AS_SEQUENCE;

            auto localAsField = sesCfgs.get<config::BgpNeighborSession::LOCAL_AS>();
            auto localProps = localAsField.hasValue() ? localAsField.load().props() : 0;
            const uint32_t confedId = getConfedId();

            if (!localAsField.hasValue())
                seg.asns.push_back(confedId);
            else if (localProps.test(config::bgp::BgpLocalAsProps::REPLACE_AS))
                seg.asns.push_back(localAsField.load().as());
            else if (localProps.test(config::bgp::BgpLocalAsProps::NO_PREPEND))
                seg.asns.push_back(confedId);
            else
            {
                seg.asns.push_back(localAsField.load().as());
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

    /**
     * @brief Withdraws the previously originated default route from a peer.
     *
     * Sends a BGP UPDATE withdrawing the AFI-appropriate default prefix (0.0.0.0/0
     * or ::/0). A no-op if no default route was previously sent to this peer (i.e.,
     * the peer RID is not in @ref defaultOriginatedPeers). Also a no-op for non-IP
     * prefix NLRI types.
     *
     * @param session  Peer session to withdraw the default route from.
     *
     * @see sendDefaultOriginate
     */
    void withdrawDefaultOriginate(Session& session)
    {
        if constexpr (!types::IsIPPrefix<NlriT>)
            return;

        const uint32_t peerRid = session.getPeerRid();
        if (!defaultOriginatedPeers.erase(peerRid))
            return;

        NlriT defaultNlri{};
        if constexpr (requires { defaultNlri.af; })
        {
            if constexpr (N::afi.afi == BGP_AFI_IPV6)
                defaultNlri.af = types::AddressFamily::IPv6;
            else
                defaultNlri.af = types::AddressFamily::IPv4;
        }

        BuildUpdate<NlriT> withdraw;
        withdraw.withdrawn.push_back({defaultNlri, 0});
        session.sendUpdate<N>(withdraw);
    }

private:
    /**
     * @brief Sends all currently active aggregate announcements to a newly established peer.
     *
     * Called from @ref onPeerEstablished and @ref refreshPeer to synchronise a new or
     * refreshing peer with the current aggregate state. Skips peers for which this AFI
     * is not negotiated or not activated.
     *
     * @param session  Peer session to send aggregates to.
     *
     * @see sendAggregateToPeer
     */
    void sendActiveAggregatesToPeer(Session& session)
    {
        if constexpr (!types::IsIPPrefix<NlriT>)
            return;
        if (!session.getNegotiated().activeFamilies.count(family))
            return;
        NeighborAf* nbrAf = session.neighbor.findAfNeighbor(family);
        if (!nbrAf || !nbrAf->configs.get<config::BgpNeighbor::ACTIVATE>().load())
            return;

        for (auto& [aggNlri, state] : aggregateStates)
            if (state.active)
                sendAggregateToPeer(aggNlri, state, session);
    }

    /**
     * @brief Checks whether a more-specific prefix is suppressed by a summary-only aggregate.
     *
     * Iterates all configured aggregate addresses whose `summary-only` flag is set. If
     * `nlri` is a more-specific subnet of an active aggregate, it must be withheld from
     * outbound UPDATEs.
     *
     * @param nlri  Candidate outbound prefix to check.
     * @return `true` if `nlri` is suppressed by an active summary-only aggregate.
     */
    bool aggregateSuppressed(const NlriT& nlri)
    {
        bool suppressed = false;
        configs.get<config::BgpAddressFamily::AGGREGATE_ADDRESS>().readEach(
            [&](const config::BgpAggregateAddress& aggr)
            {
                if (!aggr.summaryOnly())
                    return;
                const NlriT aggNlri{aggr.prefix()};
                auto stateIt = aggregateStates.find(aggNlri);
                if (stateIt == aggregateStates.end() || !stateIt->second.active)
                    return;
                if (nlri.prefixLength > aggNlri.prefixLength && aggNlri.contains(nlri))
                {
                    suppressed = true;
                    return;
                }
            }
        );
        return suppressed;
    }

    /**
     * @brief Arms the aggregate recompute timer if it is not already running.
     *
     * Uses BGP_AGGREGATE_TIMER to debounce rapid Loc-RIB changes. The timer fires
     * once after the configured delay and calls @ref recomputeAllAggregates.
     *
     * @note Only meaningful for IP prefix NLRI types; called conditionally via `if constexpr`.
     */
    void scheduleAggregateRecompute()
    {
        if (aggregateTimerId != 0)
            return;
        uint16_t delay = configs.get<config::BgpAddressFamily::BGP_AGGREGATE_TIMER>().load();
        auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(delay);
        aggregateTimerId = ScopeAccessor::getScheduler(scope).postAfter(expiry,
            [this](uint32_t) { aggregateTimerId = 0; recomputeAllAggregates(); });
    }

    /**
     * @brief Removes stale aggregate state entries and recomputes all configured aggregates.
     *
     * First withdraws and removes any @ref AggregateState entries that no longer have a
     * corresponding entry in the AGGREGATE_ADDRESS config. Then calls
     * @ref recomputeAggregate for each currently configured aggregate.
     *
     * @note A no-op for non-IP-prefix NLRI types.
     */
    void recomputeAllAggregates()
    {
        if constexpr (!types::IsIPPrefix<NlriT>)
            return;

        std::vector<config::BgpAggregateAddress> cfgs =
            configs.get<config::BgpAddressFamily::AGGREGATE_ADDRESS>().get();

        for (auto it = aggregateStates.begin(); it != aggregateStates.end(); )
        {
            bool found = std::any_of(cfgs.begin(), cfgs.end(), [&](const auto& cfg) {
                return cfg.prefix() == it->first;
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

    /**
     * @brief Computes and originates one aggregate prefix from Loc-RIB contributors.
     *
     * Finds all Loc-RIB entries that are more-specific subnets of `cfg`'s aggregate
     * prefix. If none exist, withdraws the aggregate and marks it inactive. Otherwise,
     * builds a PathAttribute by taking the worst ORIGIN across contributors and either
     * collecting all contributor AS numbers into an AS_SET (when `as-confed-set` is
     * configured) or setting ATOMIC_AGGREGATE. Stamps the AGGREGATOR attribute with
     * the local AS and router ID, stores the result in @ref AggregateState, and
     * advertises it to all peers via @ref sendAggregateToAllPeers.
     *
     * @param cfg  Aggregate address configuration tuple (prefix, as-set flag, summary-only flag).
     */
    void recomputeAggregate(const config::BgpAggregateAddress& cfg)
    {
        const NlriT aggNlri{cfg.prefix()};
        const bool buildAsSet = cfg.asConfedSet();

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
            const uint32_t localAs = ScopeAccessor::getAsNum(scope);
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
            agg.asn = ScopeAccessor::getAsNum(scope);
            agg.speaker = types::IPAddress(ScopeAccessor::getRid(scope));
            pa.attrs.asAggregator = std::move(agg);
        }

        state.basePa = pa;
        sendAggregateToAllPeers(aggNlri, state);
        state.active = true;
    }

    /**
     * @brief Advertises an active aggregate to all established and activated peers.
     *
     * Iterates every neighbor; skips those without an established session, without this
     * AFI negotiated, or without ACTIVATE configured. Calls @ref sendAggregateToPeer
     * for each eligible peer.
     *
     * @param aggNlri  Aggregate prefix to advertise.
     * @param state    Aggregate state holding the pre-policy PathAttribute.
     */
    void sendAggregateToAllPeers(const NlriT& aggNlri, AggregateState& state)
    {
        ScopeAccessor::getNtable(scope).forEachSession([&](Session& session) {
            if (!session.established())
                return;
            if (!session.getNegotiated().activeFamilies.count(family))
                return;
            NeighborAf* nbrAf = session.neighbor.findAfNeighbor(family);
            if (!nbrAf || !nbrAf->configs.get<config::BgpNeighbor::ACTIVATE>().load())
                return;

            sendAggregateToPeer(aggNlri, state, session);
        });
    }

    /**
     * @brief Sends one aggregate announcement to a specific peer.
     *
     * Copies the aggregate's base PathAttribute, applies @ref applyAggregateEgressPolicy
     * for this session, and sends a single-NLRI UPDATE.
     *
     * @param aggNlri  Aggregate prefix to announce.
     * @param state    Aggregate state holding the pre-policy PathAttribute.
     * @param session  Target peer session.
     */
    void sendAggregateToPeer(const NlriT& aggNlri, AggregateState& state, Session& session)
    {
        PathAttribute pa = state.basePa;
        egress.applyAggregateEgressPolicy(pa, session);

        BuildUpdate<NlriT> update;
        typename BuildUpdate<NlriT>::Announcement ann;
        ann.attrs = std::move(pa);
        ann.nlri.push_back({aggNlri, 0});
        update.announcements.push_back(std::move(ann));
        session.sendUpdate<N>(update);
    }

    /**
     * @brief Withdraws an aggregate prefix from all established peers.
     *
     * Iterates every neighbor with an established session for which this AFI is
     * negotiated and sends a single-NLRI withdrawal UPDATE. Peers for which the AFI
     * is not negotiated are silently skipped.
     *
     * @param aggNlri  Aggregate prefix to withdraw.
     */
    void withdrawAggregate(const NlriT& aggNlri)
    {
        ScopeAccessor::getNtable(scope).forEachSession([&](Session& session) {
            if (!session.established())
                return;
            if (!session.getNegotiated().activeFamilies.count(family))
                return;

            BuildUpdate<NlriT> withdraw;
            withdraw.withdrawn.push_back({aggNlri, 0});
            session.sendUpdate<N>(withdraw);
        });
    }

    /**
     * @brief Context carried through a RIB watch callback to identify the owning NHT watch.
     *
     * Allocated inside @ref NhtEntry and passed to the RIB as the opaque `ctx` pointer.
     * The callback captures `self` and `bgpSched` by value so it can post back to the
     * BGP scheduler without touching any member state directly from the RIB thread.
     *
     * @see nhtCallback, registerNht
     */
    struct NhtCtx
    {
        AddressFamilyInstance* self;
        types::IPAddress              nextHop;
        core::ProcessQueue        bgpSched;
    };

    /**
     * @brief Per-next-hop NHT tracking entry.
     * @ingroup BGP_AF
     *
     * Groups all installed NLRIs that share a common next-hop address under a single
     * RIB watch. When reachability changes, all associated NLRIs are queued in
     * @ref pendingNhtRecompute and processed after the NHT recompute delay.
     */
    struct NhtEntry
    {
        uint32_t                  watchId   = 0;
        bool                      isV6      = false;
        bool                      reachable = true; ///< Current reachability of this next-hop; updated by @ref onNhtChange.
        std::unordered_set<NlriT> nlris;
        std::optional<NhtCtx>     ctx;
    };

    /**
     * @brief RIB watch callback invoked when a next-hop's best route changes.
     *
     * Called on the RIB's thread. Posts a lambda back to the BGP scheduler that calls
     * @ref onNhtChange with the new reachability state. Returning `false` keeps the
     * watch registered for future changes.
     *
     * @tparam Addr  Address type of the RIB watcher (`uint32_t` for IPv4, `__uint128_t` for IPv6).
     * @param cctx   Callback context provided by the RIB, containing the opaque `ctx` pointer
     *               and the new best-route pointer (null when the next-hop becomes unreachable).
     * @return `false` to keep the watch alive.
     *
     * @see registerNht, onNhtChange
     */
    template <typename Addr>
    static bool nhtCallback(core::RouteWatcher<Addr>::CallbackCtx& cctx)
    {
        auto* ntx     = static_cast<NhtCtx*>(cctx.ctx);
        bool  reach   = (cctx.newBest != nullptr);
        types::IPAddress nh  = ntx->nextHop;
        ntx->bgpSched.post([self = ntx->self, nh, reach]() {
            self->onNhtChange(nh, reach);
        });
        return false;
    }

    /**
     * @brief Registers a next-hop tracking watch for a newly installed route.
     *
     * If BGP_NEXT_HOP_TRACKING is disabled, this is a no-op. Otherwise, an @ref NhtEntry
     * is created or updated for `nh`, and a RIB route watch is registered via
     * `RoutingTable::watchAddress`. Multiple NLRIs sharing the same next-hop share a
     * single watch. Records the NLRI-to-next-hop mapping in @ref nlriToNextHop for
     * efficient reversal during @ref unregisterNht.
     *
     * @param nlri  Installed NLRI whose next-hop is to be tracked.
     * @param nh    Next-hop address resolved from the route's path attributes.
     */
    void registerNht(const NlriT& nlri, const types::IPAddress& nh)
    {
        if (!configs.get<config::BgpAddressFamily::BGP_NEXT_HOP_TRACKING>().load())
            return;

        auto& rt = ScopeAccessor::getRoutingInstance(scope).getRib();

        auto [it, inserted] = nhtTable.emplace(nh, NhtEntry{});
        NhtEntry& entry = it->second;
        entry.nlris.insert(nlri);

        if (inserted)
        {
            entry.isV6 = nh.isIPv6();
            entry.ctx.emplace(NhtCtx{this, nh, ScopeAccessor::getScheduler(scope).ref()});
            if (nh.isIPv6())
                entry.watchId = rt.watchAddress(nh.v6(), &entry.ctx.value(), nhtCallback<__uint128_t>);
            else
                entry.watchId = rt.watchAddress(nh.v4(), &entry.ctx.value(), nhtCallback<uint32_t>);
        }

        nlriToNextHop[nlri] = nh;
    }

    /**
     * @brief Removes the NHT association for a withdrawn route.
     *
     * Looks up the next-hop for `nlri` in @ref nlriToNextHop, removes `nlri` from the
     * corresponding @ref NhtEntry, and cancels the RIB watch and destroys the entry
     * if no other NLRIs share the same next-hop.
     *
     * @param nlri  Withdrawn NLRI whose next-hop tracking is to be removed.
     */
    void unregisterNht(const NlriT& nlri)
    {
        auto nhIt = nlriToNextHop.find(nlri);
        if (nhIt == nlriToNextHop.end()) return;

        types::IPAddress nh = nhIt->second;
        nlriToNextHop.erase(nhIt);

        auto entIt = nhtTable.find(nh);
        if (entIt == nhtTable.end()) return;

        NhtEntry& entry = entIt->second;
        entry.nlris.erase(nlri);

        if (entry.nlris.empty())
        {
            if (entry.watchId)
            {
                auto& rt = ScopeAccessor::getRoutingInstance(scope).getRib();
                if (entry.isV6) rt.unwatchAddress<__uint128_t>(entry.watchId);
                else rt.unwatchAddress<uint32_t>(entry.watchId);
            }
            nhtTable.erase(entIt);
        }
    }

    /**
     * @brief Cancels all NHT watches and clears the NHT tables on teardown.
     *
     * Called from the destructor to ensure all RIB watches are deregistered before
     * the AFI instance is destroyed. Clears both @ref nhtTable and @ref nlriToNextHop.
     */
    void clearNhtWatches()
    {
        if (nhtTable.empty()) return;
        auto& rt = ScopeAccessor::getRoutingInstance(scope).getRib();
        for (auto& [nh, entry] : nhtTable)
        {
            if (entry.watchId)
            {
                if (entry.isV6) rt.unwatchAddress<__uint128_t>(entry.watchId);
                else rt.unwatchAddress<uint32_t>(entry.watchId);
            }
        }
        nhtTable.clear();
        nlriToNextHop.clear();
    }

    /**
     * @brief Determines the administrative distance to assign to an inbound route.
     *
     * Selects among DISTANCE_BGP_EXTERNAL, DISTANCE_BGP_INTERNAL, DISTANCE_BGP_LOCAL
     * (or their MBGP equivalents for non-IPv4-unicast families) based on whether the
     * route is eBGP, iBGP, or locally originated. For IP prefix NLRI types, also
     * evaluates the DISTANCE_RANGE per-prefix override list; the first matching range
     * replaces the base distance.
     *
     * @param route  Route to classify (examines `sourceNeighbor` and `ebgp` fields).
     * @param nlri   Prefix associated with `route`; used for DISTANCE_RANGE matching.
     * @return Administrative distance to assign when installing into the RIB.
     */
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
                ? configs.get<config::BgpAddressFamily::DISTANCE_MBGP_LOCAL>().load()
                : configs.get<config::BgpAddressFamily::DISTANCE_BGP_LOCAL>().load();
        }
        else if (route.ebgp)
        {
            dist = isMbgp
                ? configs.get<config::BgpAddressFamily::DISTANCE_MBGP_EXTERNAL>().load()
                : configs.get<config::BgpAddressFamily::DISTANCE_BGP_EXTERNAL>().load();
        }
        else
        {
            dist = isMbgp
                ? configs.get<config::BgpAddressFamily::DISTANCE_MBGP_INTERNAL>().load()
                : configs.get<config::BgpAddressFamily::DISTANCE_BGP_INTERNAL>().load();
        }

        // DISTANCE_RANGE: per-prefix administrative distance override.
        if constexpr (types::IsIPPrefix<NlriT>)
        {
            configs.get<config::BgpAddressFamily::DISTANCE_RANGE>().readEach(
                [&](const config::BgpDistanceRange& range)
                {
                    uint8_t rangeDist = range.distance();
                    auto pfx = range.prefix();
                    types::IPPrefix nlriAsPfx(nlri.addr, nlri.prefixLength);
                    if (pfx == nlriAsPfx ||
                        (pfx.prefixLength <= nlri.prefixLength && pfx.contains(nlriAsPfx)))
                    {
                        dist = rangeDist;
                        return;
                    }
                }
            );
        }

        return dist;
    }

    /**
     * @brief Assembles an NlriInstall descriptor from a Loc-RIB entry.
     *
     * Fetches the route's path attributes, computes the administrative distance via
     * @ref computeAdminDistance, sets the metric from the MED attribute (or
     * DEFAULT_METRIC for locally-originated routes when MED is absent), and records
     * the BGP_RECURSIVE_HOST flag. The resulting descriptor is passed to the NLRI
     * policy's install methods.
     *
     * @param route  Loc-RIB entry to build the install descriptor for.
     * @return Populated NlriInstall descriptor ready to pass to the NLRI policy.
     */
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
            auto defMetricField = configs.get<config::BgpAddressFamily::DEFAULT_METRIC>();
            if (defMetricField.hasValue())
                install.metric = defMetricField.load();
        }

        install.recursiveHost = configs.get<config::BgpAddressFamily::BGP_RECURSIVE_HOST>().load();

        return install;
    }

    /**
     * @brief Removes a BGP route directly from the global RIB by prefix, bypassing the NLRI policy.
     *
     * Used when withdrawing a locally-originated network-command route that was installed
     * via @ref installToRib after @ref onNetworkRibChanged. Dispatches to the appropriate
     * RIB `removeRoute` overload based on the AFI (IPv4 or IPv6).
     *
     * A no-op for non-IP-prefix NLRI types.
     *
     * @param nlri  Prefix to remove from the global RIB.
     */
    void withdrawFromRibDirect(const NlriT& nlri)
    {
        if constexpr (!types::IsIPPrefix<NlriT>)
            return;

        auto& rt           = ScopeAccessor::getRoutingInstance(scope).getRib();
        const uint32_t pid = ScopeAccessor::getAsNum(scope);

        if constexpr (N::afi.afi == BGP_AFI_IPV4)
            rt.removeRoute(nlri.addr, nlri.prefixLength, core::RouteSource::BGP, pid);
        else if constexpr (N::afi.afi == BGP_AFI_IPV6)
            rt.removeRoute(nlri.addr, nlri.prefixLength, core::RouteSource::BGP, pid);
    }

    /**
     * @brief Context for a network-command RIB watch callback.
     *
     * Allocated inside @ref NetworkWatchEntry and passed as the opaque `ctx` pointer to
     * the RIB watcher. Carries the NLRI being watched and a scheduler reference so that
     * @ref networkWatchCallback can safely post back to the BGP thread.
     *
     * @see networkWatchCallback, syncNetworkRoutes
     */
    struct NetworkWatchCtx
    {
        AddressFamilyInstance* self;
        NlriT                  nlri;
        core::ProcessQueue        bgpSched;
    };

    /**
     * @brief Per-prefix network-command watch state.
     * @ingroup BGP_AF
     *
     * Holds the RIB watch ID and address-family flag for one `network` prefix. The
     * embedded @ref NetworkWatchCtx must remain stable in memory for the lifetime of
     * the watch; it is stored as `std::optional` to allow in-place construction
     * without requiring a heap allocation per prefix.
     */
    struct NetworkWatchEntry
    {
        uint32_t                   watchId = 0;
        bool                       isV6    = false;
        std::optional<NetworkWatchCtx> ctx;
    };

    /**
     * @brief RIB watch callback for network-command prefix reachability changes.
     *
     * Invoked on the RIB thread when a watched network prefix appears or disappears.
     * Posts a lambda to the BGP scheduler that calls @ref onNetworkRibChanged with the
     * prefix and new reachability state. Returning `false` keeps the watch registered.
     *
     * @tparam Addr  Address type of the RIB watcher (`uint32_t` or `__uint128_t`).
     * @param cctx   Callback context from the RIB; `newBest != nullptr` means reachable.
     * @return `false` to keep the watch alive.
     *
     * @see syncNetworkRoutes, onNetworkRibChanged
     */
    template <typename Addr>
    static bool networkWatchCallback(typename core::RouteWatcher<Addr>::CallbackCtx& cctx)
    {
        auto* ctx  = static_cast<NetworkWatchCtx*>(cctx.ctx);
        bool reach = (cctx.newBest != nullptr);
        NlriT nlri = ctx->nlri;
        ctx->bgpSched.post([self = ctx->self, nlri, reach]() {
            self->onNetworkRibChanged(nlri, reach);
        });
        return false; // keep watch alive
    }

    /**
     * @brief Reacts to a network-command prefix becoming reachable or unreachable in the RIB.
     *
     * When `reachable` is `true`, synthesises a locally-originated @ref InboundRoute
     * with ORIGIN=IGP (and DEFAULT_METRIC as MED if configured) and inserts it into
     * @ref networkLocalRoutes. When `reachable` is `false`, removes the entry. In both
     * cases calls @ref recomputeNlri so the Loc-RIB and Adj-RIB-Out are updated.
     *
     * @param nlri      Network-command prefix whose reachability changed.
     * @param reachable `true` if the prefix is now reachable; `false` if it disappeared.
     */
    void onNetworkRibChanged(const NlriT& nlri, bool reachable)
    {
        if (reachable)
        {
            auto& attrMgr = ScopeAccessor::getAttrMgr(scope);

            PathAttribute pa{};
            pa.attrs.origin  = BGP_ORIGIN_IGP;
            pa.path.family   = family;

            // Apply DEFAULT_METRIC for locally-originated routes.
            auto defMetricField = configs.get<config::BgpAddressFamily::DEFAULT_METRIC>();
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

    /**
     * @brief Reconciles network-command RIB watches with the current NETWORK configuration.
     *
     * Removes watches for prefixes that have been removed from the NETWORK config (and
     * withdraws any locally-originated route for them), then adds watches for newly
     * configured prefixes. Should be called once at construction and again whenever the
     * NETWORK config key changes.
     *
     * @note A no-op for non-IP-prefix NLRI types.
     */
    void syncNetworkRoutes()
    {
        if constexpr (!types::IsIPPrefix<NlriT>)
            return;

        // Snapshot the currently configured prefixes.
        std::vector<NlriT> configured;
        configs.get<config::BgpAddressFamily::NETWORK>().readEach(
            [&](const config::BgpNetwork& net)
            {
                configured.push_back(NlriT{net.prefix()});
            });

        auto& rt = ScopeAccessor::getRoutingInstance(scope).getRib();

        // Remove watches for prefixes no longer in the config.
        for (auto it = networkWatches.begin(); it != networkWatches.end(); )
        {
            bool found = std::any_of(configured.begin(), configured.end(),
                [&](const NlriT& p) { return p == it->first; });
            if (!found)
            {
                if (it->second.watchId)
                {
                    if constexpr (N::afi.afi == BGP_AFI_IPV6)
                        rt.template unwatchAddress<__uint128_t>(it->second.watchId);
                    else
                        rt.template unwatchAddress<uint32_t>(it->second.watchId);
                }
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
            entry.ctx.emplace(NetworkWatchCtx{this, pfx, ScopeAccessor::getScheduler(scope).ref()});

            if constexpr (N::afi.afi == BGP_AFI_IPV6)
            {
                entry.isV6    = true;
                entry.watchId = rt.watchRoute(pfx.addr, pfx.prefixLength,
                                              &entry.ctx.value(), networkWatchCallback<__uint128_t>);
            }
            else
            {
                entry.isV6    = false;
                entry.watchId = rt.watchRoute(pfx.addr, pfx.prefixLength,
                                              &entry.ctx.value(), networkWatchCallback<uint32_t>);
            }
        }
    }

    /**
     * @brief Cancels all network-command RIB watches and clears local route state on teardown.
     *
     * Called from the destructor to deregister all watches registered by
     * @ref syncNetworkRoutes before the instance is destroyed. Clears both
     * @ref networkWatches and @ref networkLocalRoutes.
     */
    void clearNetworkWatches()
    {
        if (networkWatches.empty())
            return;

        auto& rt = ScopeAccessor::getRoutingInstance(scope).getRib();
        for (auto& [nlri, entry] : networkWatches)
        {
            if (entry.watchId)
            {
                if (entry.isV6) rt.unwatchAddress<__uint128_t>(entry.watchId);
                else rt.unwatchAddress<uint32_t>(entry.watchId);
            }
        }

        networkWatches.clear();
        networkLocalRoutes.clear();
    }

    /**
     * @brief Updates next-hop reachability state and queues affected NLRIs for recompute.
     *
     * Called on the BGP scheduler (posted by @ref nhtCallback) when a watched next-hop's
     * best route changes. If the reachability state is unchanged, this is a no-op.
     * Otherwise, all NLRIs in the @ref NhtEntry's `nlris` set are inserted into
     * @ref pendingNhtRecompute and the NHT recompute timer is armed.
     *
     * @param nh        Next-hop address whose reachability changed.
     * @param reachable New reachability state.
     */
    void onNhtChange(const types::IPAddress& nh, bool reachable)
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

    /**
     * @brief Arms the NHT recompute timer with the configured BGP_NEXT_HOP_TRIGGER_DELAY.
     *
     * Debounces rapid next-hop flaps by coalescing multiple reachability changes into a
     * single recompute pass. Defaults to a 5-second delay when BGP_NEXT_HOP_TRIGGER_DELAY
     * is not set. The timer fires once and calls @ref processNhtPending.
     */
    void scheduleNhtRecompute()
    {
        if (nhtTimerId != 0) return;

        uint16_t delaySecs = configs.get<config::BgpAddressFamily::BGP_NEXT_HOP_TRIGGER_DELAY>().load();

        nhtTimerId = ScopeAccessor::getScheduler(scope).postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(delaySecs),
            [this](uint32_t) { nhtTimerId = 0; processNhtPending(); });
    }

    /**
     * @brief Drains the pending NHT recompute set after the trigger delay expires.
     *
     * Moves @ref pendingNhtRecompute to a local set to prevent re-entrant insertions
     * from corrupting the iteration, then calls @ref recomputeNlri for each pending NLRI.
     */
    void processNhtPending()
    {
        std::unordered_set<NlriT> pending = std::move(pendingNhtRecompute);
        for (const NlriT& nlri : pending)
            recomputeNlri(nlri);
    }

    /**
     * @brief Removes Adj-RIB-In entries that remain stale after a BORR/EORR cycle.
     *
     * Called when either the STALEPATH or MAX-EOR timer fires (or immediately on EORR).
     * Any NLRI still in `stalePeerNlris[peerRid]` was not re-advertised by the peer
     * during the route-refresh exchange and is therefore treated as withdrawn. Affected
     * Loc-RIB entries are removed and @ref recomputeNlri is called for each touched NLRI.
     *
     * @param peerRid  Router ID of the peer whose stale routes are to be purged.
     *
     * @see onPeerBorr, onPeerEorr, cancelStaleTimers
     */
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

    /**
     * @brief Cancels the STALEPATH and MAX-EOR timers for a peer and clears its stale set.
     *
     * Called from @ref onPeerEorr to cancel in-flight timers when EORR arrives before
     * they fire, and from @ref invalidatePeer during session teardown.
     *
     * @param peerRid  Router ID of the peer whose stale-path timers are to be cancelled.
     */
    void cancelStaleTimers(uint32_t peerRid)
    {
        auto it = staleTimers.find(peerRid);
        if (it == staleTimers.end()) return;

        auto& sched = ScopeAccessor::getScheduler(scope);
        if (it->second.stalepath) sched.cancel(it->second.stalepath);
        if (it->second.maxEor)    sched.cancel(it->second.maxEor);
        staleTimers.erase(it);
        stalePeerNlris.erase(peerRid);
    }

private:

    PreAdjRibInTable<NlriT> preAdjRibIn;   ///< Pre-policy Adj-RIB-In; stored per peer for soft-reconfiguration inbound replay.
    AdjRibInTable<NlriT>   adjRibIn;      ///< Post-policy Adj-RIB-In; keyed by peer RID then (NLRI, path-id).
    LocRibTable<NlriT>     locRib;        ///< Loc-RIB: best path per prefix after decision process.
    AdjRibOutTable<NlriT>  adjRibOut;     ///< Adj-RIB-Out: per-peer egress state keyed by (peer RID, NLRI).

    std::unordered_map<uint32_t, MraiState> mraiState;        ///< MRAI rate-limit state per peer RID.
    uint32_t mraiBypassPeer = 0;                              ///< Peer RID currently draining via MRAI timer; 0 when not active.

    std::unordered_set<uint32_t> defaultOriginatedPeers;      ///< Set of peer RIDs to which a default route has been sent.
    std::unordered_map<NlriT, AggregateState> aggregateStates; ///< Per-aggregate-prefix origination state.
    uint32_t aggregateTimerId = 0;                             ///< Timer ID for the deferred aggregate recompute; 0 when not armed.

    std::unordered_map<types::IPAddress, NhtEntry>  nhtTable;         ///< NHT entries keyed by next-hop address.
    std::unordered_map<NlriT,    types::IPAddress>  nlriToNextHop;    ///< Maps each installed NLRI to its tracked next-hop address.
    std::unordered_set<NlriT>                       pendingNhtRecompute; ///< NLRIs awaiting recompute after a next-hop reachability change.
    uint32_t                                        nhtTimerId = 0;   ///< Timer ID for the NHT recompute delay; 0 when not armed.

    std::unordered_map<NlriT, NetworkWatchEntry>   networkWatches;     ///< Per-prefix RIB watches for network-command prefixes.
    std::unordered_map<NlriT, InboundRoute<NlriT>> networkLocalRoutes; ///< Locally-originated InboundRoutes injected by the network command.

    // Enhanced Route Refresh (RFC 7313) stale-path tracking.
    /**
     * @brief Holds the STALEPATH and MAX-EOR timer IDs for one peer's route-refresh cycle.
     *
     * Both timers are armed by @ref onPeerBorr and cancelled by @ref cancelStaleTimers.
     * Either timer firing triggers @ref purgeStalePeer for the associated peer RID.
     */
    struct StaleTimerIds { uint32_t stalepath = 0; uint32_t maxEor = 0; };
    std::unordered_map<uint32_t, StaleTimerIds>             staleTimers;    ///< Per-peer STALEPATH and MAX-EOR timer IDs.
    std::unordered_map<uint32_t, std::unordered_set<NlriT>> stalePeerNlris; ///< Per-peer set of NLRIs marked stale after a BORR.

    N                 policy;              ///< NLRI policy: drives RIB install/withdraw calls and defines the prefix type.
    IgpMetricResolver igpMetricResolver;   ///< Callable that resolves IGP cost to a next-hop; returns UINT64_MAX when unset or unreachable.
    BgpScope&       scope;               ///< Reference to the owning BgpScope; provides config, scheduler, and neighbor table.
    AfiSafi           family;             ///< AFI/SAFI this instance manages.

    config::BgpAddressFamilyRegistry& configs; ///< Registry reference for address-family configuration.

    EgressPolicy<N> egress; ///< Reusable egress path-attribute transforms for this AF.
    DirtyState      dirty;  ///< Debounced pending config-apply work; drained by @ref flushDirty.
};
} // namespace routing::bgp

#endif // BGP_ADDRESS_FAMILY_H

