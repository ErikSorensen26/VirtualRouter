// Dampening.cpp — RFC 2439 BGP Route Dampening

#include "Dampening.h"

namespace routing::bgp
{

static double decayFactor(double halfLifeSecs, double elapsedSecs) noexcept
{
    return std::exp(-std::log(2.0) * elapsedSecs / halfLifeSecs);
}

static std::chrono::steady_clock::time_point addSeconds(
    std::chrono::steady_clock::time_point tp, double secs)
{
    using namespace std::chrono;
    return tp + duration_cast<steady_clock::duration>(duration<double>(secs));
}

void DampenState::decayTo(double halfLifeSecs, std::chrono::steady_clock::time_point now)
{
    if (lastUpdate == std::chrono::steady_clock::time_point{})
    {
        lastUpdate = now;
        return;
    }
    double elapsed = std::chrono::duration<double>(now - lastUpdate).count();
    if (elapsed > 0.0)
    {
        penalty *= decayFactor(halfLifeSecs, elapsed);
        lastUpdate = now;
    }
}

bool DampenState::onAnnounce(const DampenParams& p, std::chrono::steady_clock::time_point now)
{
    decayTo(p.halfLifeSecs, now);

    if (everWithdrawn && !pendingReuse)
    {
        penalty += 1000.0;
        if (penalty > p.ceiling)
            penalty = p.ceiling;
    }

    if (!suppressed && penalty > p.suppress)
    {
        suppressed     = true;
        suppressExpiry = addSeconds(now, p.maxSuppressSecs);
    }

    return suppressed;
}

bool DampenState::onWithdraw(const DampenParams& p, std::chrono::steady_clock::time_point now)
{
    decayTo(p.halfLifeSecs, now);

    everWithdrawn = true;
    penalty += 1000.0;
    if (penalty > p.ceiling)
        penalty = p.ceiling;

    if (!suppressed && penalty > p.suppress)
    {
        suppressed     = true;
        suppressExpiry = addSeconds(now, p.maxSuppressSecs);
    }

    return suppressed;
}

bool DampenState::checkReuse(const DampenParams& p, std::chrono::steady_clock::time_point now)
{
    if (!suppressed)
        return false;

    decayTo(p.halfLifeSecs, now);

    if (penalty <= p.reuse || now >= suppressExpiry)
    {
        suppressed = false;
        return true; // just un-suppressed
    }

    return false;
}

} // namespace routing::bgp
