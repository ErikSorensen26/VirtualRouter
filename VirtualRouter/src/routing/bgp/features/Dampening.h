// Dampening.h — RFC 2439 BGP Route Dampening

#pragma once

#include <chrono>
#include <cmath>

namespace routing::bgp
{

struct DampenParams
{
    uint16_t halfLifeSecs;
    uint16_t reuse;
    uint16_t suppress;
    uint16_t maxSuppressSecs;
    double ceiling;
};

/* Per-prefix dampening state.
 *
 * Lifecycle:
 *   onWithdraw()  — called when a prefix transitions from reachable → unreachable in Loc-RIB.
 *   onAnnounce()  — called when a prefix transitions from unreachable → reachable in Loc-RIB
 *                   (i.e. a new best candidate appeared after the route had been absent).
 *   checkReuse()  — called periodically; returns true if the route just became eligible for
 *                   re-advertisement (penalty decayed below the reuse threshold).
 *   isStale()     — returns true when the entry carries no useful history and can be discarded.
 *
 * pendingReuse is set by the reuse-scan timer before calling recomputeNlri() so that the
 * reappearance of the route is not counted as another flap.
 */
struct DampenState
{
    double penalty      = 0.0;
    bool   suppressed   = false;
    bool   everWithdrawn = false;
    bool   pendingReuse = false;

    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::steady_clock::time_point suppressExpiry;

    // Decay penalty to 'now'.
    void decayTo(double halfLifeSecs, std::chrono::steady_clock::time_point now);

    // Route becoming reachable (new announcement or re-announcement after absence).
    // Returns true if the route should remain suppressed (caller must not install to Loc-RIB).
    bool onAnnounce(const DampenParams& p, std::chrono::steady_clock::time_point now);

    // Route becoming unreachable.
    // Returns true if the route is now suppressed (no further effect; caller still withdraws).
    bool onWithdraw(const DampenParams& p, std::chrono::steady_clock::time_point now);

    // Periodic reuse check.  Returns true if the route just transitioned from suppressed to
    // eligible; caller should set pendingReuse = true and re-trigger recomputeNlri().
    bool checkReuse(const DampenParams& p, std::chrono::steady_clock::time_point now);

    // True when the entry carries negligible history and may be removed from the table.
    bool isStale() const noexcept { return !suppressed && penalty < 1.0; }
};

} // namespace routing::bgp
