/**
 * @file DirtyState.hpp
 * @brief Debounced config-apply state for a BGP address-family instance.
 * @ingroup BGP_AF
 *
 * Config changes do not recompute anything immediately. A registry applier marks the
 * affected BGP path attribute(s) dirty and arms a short debounce timer. When it fires,
 * the flush does a partial rebuild: for each dirty neighbor it re-derives only the dirty
 * attributes onto the already-advertised Adj-RIB-Out attribute set (leaving every clean
 * attribute untouched), batched by shared attribute set. A burst of edits collapses into
 * one pass.
 */

#ifndef BGP_DIRTY_STATE_HPP
#define BGP_DIRTY_STATE_HPP

#include <bitset>
#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_set>

#include <ControlScheduler.h>

namespace routing::bgp
{

constexpr uint32_t kBgpDebounceMs = 250;

/**
 * @brief Outbound dirtiness at BGP-path-attribute granularity.
 *
 * A config change marks the attribute(s) it rewrites, not the config field. Many fields
 * fold onto the same attribute (e.g. SEND_COMMUNITY and RPKI-state both mark COMMUNITIES;
 * REMOVE_PRIVATE_AS and local-as options both mark AS_PATH). On flush, only the marked
 * attributes are re-derived; the rest of the advertised attribute set is preserved.
 *
 * ACTIVATE and ADD_PATH are not attribute rewrites — they change membership / path
 * selection — and are handled on their own paths, kept here so one mask covers all
 * per-neighbor outbound work.
 */
enum class OutAttr : uint8_t
{
    NEXT_HOP,          ///< NEXT_HOP.
    AS_PATH,           ///< AS_PATH (prepend, private-AS strip, local-as construction).
    COMMUNITIES,       ///< COMMUNITY (RFC 1997).
    EXT_COMMUNITIES,   ///< EXTENDED_COMMUNITIES (RFC 4360).
    LARGE_COMMUNITIES, ///< LARGE_COMMUNITY (RFC 8092).
    LOCAL_PREF,        ///< LOCAL_PREF.
    REFLECTION,        ///< ORIGINATOR_ID + CLUSTER_LIST (route reflection).
    ACTIVATE,          ///< AF activation toggle (membership, not an attribute).
    ADD_PATH,          ///< ADD-PATH advertise selection (path set, not an attribute).
    COUNT
};

using OutAttrMask = std::bitset<static_cast<size_t>(OutAttr::COUNT)>;

/**
 * @brief Inbound-pipeline dirty categories (Adj-RIB-In re-evaluation).
 */
enum class InDirty : uint8_t
{
    POLICY,        ///< Inbound route-map / prefix-list / filter changed.
    ALLOWAS,       ///< ALLOWAS_IN / occurrence limit changed.
    MAX_PREFIX,    ///< Maximum-prefix limit changed.
    SOFT_RECONFIG, ///< Soft-reconfiguration toggled.
    COUNT
};

/**
 * @brief Process-AF dirty categories (Loc-RIB / decision-process re-evaluation).
 */
enum class AfDirty : uint8_t
{
    BEST_PATH,       ///< Best-path tiebreaker config changed.
    DISTANCE,        ///< Administrative-distance config changed.
    DAMPENING,       ///< Dampening config changed.
    AGGREGATE,       ///< Aggregate-address config changed.
    NETWORK,         ///< Network-statement list changed.
    MAX_PATHS,       ///< Maximum-paths (ECMP) config changed.
    ADD_PATH_SELECT, ///< AF-level ADD-PATH candidate selection changed.
    COUNT
};

inline void setBit(OutAttrMask& m, OutAttr a) noexcept { m.set(static_cast<size_t>(a)); }
inline bool testBit(const OutAttrMask& m, OutAttr a) noexcept { return m.test(static_cast<size_t>(a)); }

/**
 * @brief Pending config-driven work for one address-family instance.
 * @ingroup BGP_AF
 *
 * The per-neighbor outbound attribute masks live on each NeighborAf; this struct tracks
 * which neighbors are dirty (@ref outNeighbors) plus the AF-wide inbound / process-AF
 * work. All mutation is on the BGP scheduler thread, so no locking. @ref arm schedules
 * exactly one flush; @ref drainAfIn snapshots+clears the AF-wide bits (the neighbor masks
 * are drained individually during the flush).
 */
struct DirtyState
{
    std::bitset<static_cast<size_t>(InDirty::COUNT)> in;  ///< Pending AF-wide inbound categories.
    std::bitset<static_cast<size_t>(AfDirty::COUNT)>  af;  ///< Pending process-AF categories.

    std::unordered_set<uint32_t> inPeers;      ///< Peers needing inbound re-run; empty = all.
    std::unordered_set<uint32_t> outNeighbors; ///< Peer RIDs whose NeighborAf has dirty outbound attrs.

    uint32_t timerId = 0; ///< Debounce timer ID; 0 when no flush is armed.

    void markIn(InDirty c) noexcept { in.set(static_cast<size_t>(c)); }
    void markAf(AfDirty c) noexcept { af.set(static_cast<size_t>(c)); }
    void markInPeer(uint32_t peerRid) noexcept { if (peerRid) inPeers.insert(peerRid); }
    void markOutNeighbor(uint32_t peerRid) noexcept { outNeighbors.insert(peerRid); }

    bool testIn(InDirty c) const noexcept { return in.test(static_cast<size_t>(c)); }
    bool testAf(AfDirty c) const noexcept { return af.test(static_cast<size_t>(c)); }

    bool any() const noexcept { return in.any() || af.any() || !outNeighbors.empty(); }

    /**
     * @brief Moves the accumulated state out and resets to empty.
     *
     * The flush works from this stable snapshot while any concurrently-posted marks
     * accumulate into a fresh state (arming their own next flush). The per-neighbor
     * attribute masks are drained separately during the flush.
     */
    DirtyState drain()
    {
        DirtyState snap;
        snap.in           = in;
        snap.af           = af;
        snap.inPeers      = std::move(inPeers);
        snap.outNeighbors = std::move(outNeighbors);
        in.reset();
        af.reset();
        inPeers.clear();
        outNeighbors.clear();
        return snap;
    }

    /**
     * @brief Arms the single debounce timer if none is pending.
     */
    void arm(const core::ProcessQueue& sched, std::function<void()> flush)
    {
        if (timerId != 0)
            return;
        auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(kBgpDebounceMs);
        timerId = sched.postAfter(expiry, [this, flush = std::move(flush)](uint32_t) {
            timerId = 0;
            flush();
        });
    }
};

} // namespace routing::bgp

#endif // BGP_DIRTY_STATE_HPP
