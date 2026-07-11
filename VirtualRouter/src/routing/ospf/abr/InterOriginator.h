/**
 * @file InterOriginator.h
 * @brief Process-scoped ABR origination: inter-area summaries, ASBR reachability, and area default routes.
 */

#ifndef OSPF_INTER_ORIGINATOR_H
#define OSPF_INTER_ORIGINATOR_H

#include <cstdint>
#include <IPAddress.h>
#include <unordered_map>
#include <vector>
#include <atomic>

class Internal_OspfTest;

namespace routing::ospf
{
class OriginatorContext;
class OspfProcess;
struct OspfRouteChange;

/**
 * @brief Process-scoped originator for the ABR role (RFC 2328 §12.4.3).
 * @ingroup OSPF
 *
 * `InterOriginator` owns everything the router advertises *because it borders
 * multiple areas*: Type-3 / Inter-Area-Prefix summaries, Type-4 /
 * Inter-Area-Router (ASBR reachability) LSAs, the default summary injected
 * into stub areas, and the Type-7 default injected into NSSA areas.
 *
 * It is pure *policy*: computed bodies are submitted into the target area's
 * @ref OriginatorContext, which handles throttling, group-paced refresh, and
 * the LSDB install path.  Intra-area topology LSAs belong to
 * @ref IntraOriginator; external redistribution (ASBR role) belongs to
 * @ref ExternalOriginator.
 *
 * ## Lifecycle & Ownership
 * One instance per @ref OspfProcess, constructed with it and living for the
 * whole process lifetime regardless of whether the router currently is an ABR.
 *
 * ## Concurrency Model
 * All methods run on the owning process's single-threaded `ProcessQueue`, so
 * touching per-area origination state from this process-scoped object is safe.
 *
 * @see OspfProcess, Area, OriginatorContext, ExternalOriginator
 */
class InterOriginator
{
    friend class ::Internal_OspfTest;
public:

    /**
     * @brief Constructs the ABR originator bound to its owning process.
     *
     * Does not originate anything at construction time; origination is driven
     * by SPF results and ABR-flag changes.
     *
     * @param process The process that owns this originator.
     */
    InterOriginator(OspfProcess& process);

    // SUMMARIES
    /**
     * @brief Originates or withdraws an inter-area summary LSA for the given prefix.
     *
     * Used by ABRs to summarise intra-area prefixes into adjacent areas.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx    Origination context of the area the summary is flooded into.
     * @param lsid   Link-State ID for the summary LSA.
     * @param prefix Network prefix being summarised.
     * @param cost   Metric to advertise in the summary.
     * @param expire If true, the summary is being withdrawn.
     */
    template <typename Policy>
    void originateSummary(OriginatorContext& ctx, uint32_t lsid, const types::IPPrefix& prefix, uint32_t cost, bool expire = false);

    /**
     * @brief Re-originates inter-area summary LSAs for a list of route changes.
     *
     * Called after an SPF run when intra-area routes change.  For each changed
     * route this process is an ABR for, it re-computes and floods a Type-3
     * (OSPFv2) or Inter-Area-Prefix (OSPFv3) summary LSA into adjacent areas.
     *
     * @tparam Policy  PolicyV2 or PolicyV3.
     * @param ctx      Origination context of the area whose SPF results changed.
     * @param pathList List of route additions/withdrawals from that SPF run.
     */
    template <typename Policy>
    void reoriginateSummaries(OriginatorContext& ctx, std::vector<OspfRouteChange>& pathList);

    // ASBR REACHABILITY

    /**
     * @brief Originates an ASBR-summary LSA for the given external router.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx     Origination context of the area the LSA is flooded into.
     * @param asbr    Router ID of the ASBR being summarised.
     * @param refresh True if this is a periodic refresh.
     */
    template <typename Policy>
    void addAsbrLsa(OriginatorContext& ctx, uint32_t asbr, bool refresh = false);

    /**
     * @brief Originates or flushes an ASBR-summary LSA for a redistributed route.
     *
     * Tracks which external LSAs each ASBR contributes; when the last external
     * route from an ASBR is withdrawn, the ASBR-summary LSA is flushed.
     *
     * @param ctx    Origination context of the area tracking the ASBR.
     * @param asbr   Router ID of the ASBR that introduced the external route.
     * @param lsid   Link-State ID to use for the new LSA.
     * @param expire If true, the LSA is being withdrawn (age set to MaxAge).
     */
    template <typename Policy>
    void addExternal(OriginatorContext& ctx, uint32_t asbr, uint32_t lsid, bool expire);

    /**
     * @brief Re-originates the ASBR-summary LSAs for every ASBR tracked by the given area.
     *
     * Only NORMAL areas carry ASBR summaries; for stub/NSSA areas this is a
     * no-op.  Called from `fullRefresh()` and after topology changes that may
     * alter ASBR reachability.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx     Origination context of the area to refresh.
     */
    template <typename Policy>
    void refreshAsbrs(OriginatorContext& ctx);

    // DEFAULT ROUTES

    /**
     * @brief Re-evaluates whether the area should carry a default summary and applies the result.
     *
     * A default summary (0/0) exists when this router is an ABR and the area
     * type is STUB or TOTALLY_STUB; delegates to `setStubDefaultOriginate`.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx     Origination context of the area to re-evaluate.
     */
    template <typename Policy>
    void refreshStubDefaultOriginate(OriginatorContext& ctx);

    /**
     * @brief Re-evaluates whether the area should carry an NSSA Type-7 default and applies the result.
     *
     * The Type-7 default exists when this router is an ABR, the area type is
     * NSSA or TOTALLY_NSSA, and `nssa default-information-originate` is
     * configured; delegates to `setNssaDefaultOriginate`.
     *
     * @param ctx Origination context of the area to re-evaluate.
     */
    void refreshNssaDefaultOriginate(OriginatorContext& ctx);

    /**
     * @brief Originates or withdraws the default (0/0) summary LSA in a stub area.
     *
     * Idempotent: does nothing if the requested state already holds.  The
     * summary metric comes from the area's `default-cost` configuration.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx Origination context of the stub area.
     * @param add True to originate, false to flush.
     */
    template <typename Policy>
    void setStubDefaultOriginate(OriginatorContext& ctx, bool add);

    /**
     * @brief Originates or withdraws the NSSA Type-7 default LSA in an NSSA area.
     *
     * Idempotent: does nothing if the requested state already holds.  Metric
     * and metric type come from the area's NSSA default configuration.
     *
     * @param ctx Origination context of the NSSA area.
     * @param add True to originate, false to flush.
     */
    void setNssaDefaultOriginate(OriginatorContext& ctx, bool add);


    /**
     * @brief Allocates the next Link-State ID for OSPFv3 inter-area summary LSAs.
     *
     * OSPFv3 summary Link-State IDs carry no addressing semantics, so they are
     * handed out from a process-wide monotonic counter.
     */
    uint32_t fetchAddMonotonicIntraId();

private:

    /**
     * @brief Re-originates a single inter-area summary LSA for one route change.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx     Origination context of the area whose route changed.
     * @param path    The individual route change to summarize.
     */
    template <typename Policy>
    void reoriginateSummary(OriginatorContext& ctx, OspfRouteChange& path);

    // SUMMARY LSA TRACKING
    std::unordered_map<types::IPPrefix, uint32_t> intraLsids; ///< Maps intra-area prefix to its allocated Link-State ID for summary LSAs.
    std::atomic<uint32_t> monotonicIntraId{0}; ///< Monotonically increasing Link-State ID allocator for intra-area prefix summaries.

    OspfProcess& process;
};
}

#endif // OSPF_INTER_ORIGINATOR_H
