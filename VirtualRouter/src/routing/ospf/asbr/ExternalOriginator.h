/**
 * @file ExternalOriginator.h
 * @brief Process-scoped ASBR origination: external LSAs, NSSA translation, summary-address aggregation, and the external LSDB.
 */

#ifndef OSPF_EXTERNAL_ORIGINATOR_H
#define OSPF_EXTERNAL_ORIGINATOR_H

#include <optional>
#include "ospf/database/LsdbTypes.hpp"

class Internal_OspfTest;

namespace routing::ospf
{
class OriginatorContext;
class OspfProcess;

/**
 * @brief Parameters passed to the external LSA originator when redistributing a route into OSPF.
 * @ingroup OSPF_TOPOLOGY
 *
 * Carries all fields needed to build a Type-5 or Type-7 LSA body: the prefix,
 * metric, tag, and an optional forwarding address.
 */
struct ExternalOriginateContext
{
    uint32_t lsId;                        ///< Link State ID to use for this LSA.
    types::IPPrefix prefix;               ///< Destination prefix being redistributed.
    uint32_t metric;                      ///< External metric value.
    uint32_t tag;                         ///< Route tag (passed through transparently).
    std::optional<types::IPAddress> nextHop; ///< Forwarding address, if non-zero should be included in the LSA.
    bool metricIsE2;                      ///< False = E1 (cost accumulates); true = E2 (cost is flat).
};

/**
 * @brief Process-scoped originator for the ASBR role (RFC 2328 §12.4.4, RFC 3101).
 * @ingroup OSPF
 *
 * `ExternalOriginator` owns everything the router advertises *because it
 * redistributes routes into OSPF*: AS-External (Type-5) and NSSA-External
 * (Type-7) LSAs, the Type-7→Type-5 translation performed by an NSSA ABR, the
 * `default-information originate` default route, and `summary-address`
 * external route aggregation.  It also owns the process-wide external LSDB
 * (`externalDb`), since external LSAs are scoped to the AS rather than to a
 * single area.
 *
 * It is pure *policy*: computed bodies are submitted into each target area's
 * @ref OriginatorContext, which handles throttling, group-paced refresh, and
 * the LSDB install path.  Intra-area topology LSAs belong to
 * @ref IntraOriginator; inter-area summaries (ABR role) belong to
 * @ref InterOriginator.
 *
 * ## Lifecycle & Ownership
 * One instance per @ref OspfProcess, constructed with it and living for the
 * whole process lifetime regardless of whether the router currently is an
 * ASBR. All state, including `externalDb`, is accessed exclusively on the
 * owning process's single-threaded `ProcessQueue`.
 *
 * @see OspfProcess, Area, OriginatorContext, InterOriginator
 */
class ExternalOriginator
{
    friend class ::Internal_OspfTest;
public:
    /**
     * @brief Constructs the ASBR originator bound to its owning process.
     *
     * Does not originate anything at construction time; origination is driven
     * by redistribution events and configuration changes.
     *
     * @param proc The process that owns this originator.
     */
    ExternalOriginator(OspfProcess& proc);

    /**
     * @brief Distributes a received external LSA to all other areas (NSSA translation path).
     *
     * Called when an ABR receives an NSSA-External LSA and must translate and
     * flood it as an AS-External LSA into the non-NSSA areas of this process.
     *
     * @tparam Policy PolicyV2 or PolicyV3 — selects LSA wire format.
     * @param areaCtx Origination context of the area that received the original NSSA LSA.
     * @param lsaCtx  Context for the incoming LSA (key, header, flood info).
     * @param body    Decoded LSA body to distribute.
     */
    template <typename Policy>
    void distributeExternalLsa(const OriginatorContext& areaCtx, IncomingLsaContext& lsaCtx, const LsaBody& body);

    /**
     * @brief Originates or withdraws a single AS-External (or NSSA-External) LSA.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx    Origination context describing the redistributed route.
     * @param expire True to flush (MaxAge) the LSA rather than originate it.
     */
    template <typename Policy>
    void originateExternal(ExternalOriginateContext& ctx, bool expire);

    /**
     * @brief Batch version of @ref originateExternal for multiple routes.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctxs Vector of (context, expire) pairs to process in one shot.
     */
    template <typename Policy>
    void originateExternals(std::vector<std::pair<ExternalOriginateContext, bool>>& ctxs);

    /**
     * @brief Constructs the @ref LsaKey for a redistributed external route.
     *
     * @tparam Policy   PolicyV2 or PolicyV3.
     * @param ctx       Origination context.
     * @param isNssa    True when building a key for an NSSA-External LSA.
     * @return          The LsaKey that uniquely identifies this LSA in the LSDB.
     */
    template <typename Policy>
    LsaKey buildExternalKey(ExternalOriginateContext& ctx, bool isNssa);

    /**
     * @brief Fills in the LSA body fields for a redistributed external route.
     *
     * @tparam Policy   PolicyV2 or PolicyV3.
     * @param ctx       Origination context carrying metric, tag, and forwarding address.
     * @param[out] body External LSA body to populate.
     * @param isNssa    True when building an NSSA-External LSA body.
     */
    template <typename Policy>
    void buildExternalBody(ExternalOriginateContext& ctx, Policy::ExternalLsa& body, bool isNssa);

    // DEFAULT ROUTES

    /**
     * @brief Advertises or withdraws the OSPF default route.
     *
     * When `add` is true, originates a Type-5 (or Type-7 into NSSA areas)
     * LSA for 0.0.0.0/0 with the configured metric and metric type.
     *
     * @param add True to inject the default route, false to withdraw it.
     */
    void addDefaultRoute(bool add);

    // SUMMARY ADDRESSES

    /**
     * @brief Synchronizes the summary-address configuration with the active origination state.
     *
     * Reads the current `summary-address` CLI configuration and reconciles it
     * against the running set of summary LSAs, triggering re-origination or
     * withdrawal as needed.  Public because it is the entry point for
     * `OspfProcess::enqueueSyncSummaries()` after a config change.
     */
    void syncSummaryConfig();

    // EXTERNAL LSDB (process-scoped)
    std::unordered_map<LsaKey, std::pair<LsaHeader, LsaBody>> externalDb; ///< AS-External and NSSA-External LSAs, keyed by LsaKey.  Public for read access by the route manager; only mutate on the process scheduler thread.

private:

    /**
     * @brief Translates an NSSA external LSA into a Type-5 external LSA for flooding beyond the NSSA.
     *
     * Called on the NSSA-ABR that is responsible for translating Type-7 LSAs
     * into Type-5 LSAs.  The translated LSA is re-originated with the key
     * derived from the source NSSA LSA.
     *
     * @param ctx    Origination context of the NSSA area the LSA arrived in.
     * @param key    Key of the source NSSA LSA being translated.
     * @param lsa    Decoded body of the source NSSA LSA.
     * @param expire If true, the translation is being withdrawn.
     */
    template <typename Policy>
    void translateNssaToExternal(OriginatorContext& ctx, const LsaKey& key, const LsaBody& lsa, bool expire);

    /**
     * @brief Runtime state for a configured `summary-address` aggregation prefix.
     *
     * Tracks both the operator-supplied configuration and the live contribution
     * count derived from intra-area routes covered by the range.  When
     * `contributorCount` drops to zero the aggregate LSA is withdrawn.
     */
    struct OspfSummaryAddress {
        // Config
        bool notAdvertise = false;          ///< If true the summary is suppressed (black-hole only).
        bool nssaOnly = false;              ///< If true limit this summary to NSSA areas.
        std::optional<uint32_t> tag;        ///< Optional route tag to stamp on the originated LSA.

        // Runtime
        uint32_t contributorCount = 0;      ///< Number of more-specific routes currently covered.
        uint32_t computedMetric = 0;        ///< Best metric among contributing routes.
        bool isType2;                       ///< True if the aggregate uses a Type-2 (E2) metric.

        uint32_t lsId;                      ///< Allocated Link-State ID for the originated LSA.
        bool discardPresent = false;        ///< True if a discard (null-route) has been installed in the RIB.
    };

    /**
     * @brief Suppresses or unsuppresses more-specific external LSAs covered by a summary.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param activeSummaries The set of summary prefixes currently in effect.
     */
    template <typename Policy>
    void syncSummarySuppression(std::unordered_map<types::IPPrefix, OspfSummaryAddress>& activeSummaries);

    // SUMMARIES
    std::unordered_map<types::IPPrefix, OspfSummaryAddress> summaries; ///< Active summary-address entries keyed by aggregate prefix.

    // DEFAULT ROUTES
    std::optional<uint32_t> defaultRoute = std::nullopt; ///< Link-State ID of the currently originated `default-information originate` LSA, if any.

    std::atomic<uint32_t> monotonicExternalId{0}; ///< Monotonically increasing Link-State ID allocator for external LSAs.

    OspfProcess& process;
};
}

#endif // OSPF_EXTERNAL_ORIGINATOR_H
