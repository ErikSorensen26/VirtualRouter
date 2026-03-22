// EigrpInterfaceMetrics.cpp

#include "InterfaceMetrics.h"
#include "EigrpInterface.h"
#include "interface/configs/InterfaceConfigs.h"
#include "hardware/HardwareManager.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/EigrpTypes.hpp"
#include "eigrp/rtp/Neighbor.h"

namespace EIGRP
{
InterfaceMetrics::InterfaceMetrics(EigrpInterface& iface) : iface(iface) {}

void InterfaceMetrics::addRouteMetrics(std::vector<ReceivedRoute>& routes)
{
    uint64_t local = getLocalMetric();
    for (auto& route : routes)
    {
        if (route.delay == std::numeric_limits<uint64_t>::max())
        {
            route.feasibleDistance = route.reportedDistance = std::numeric_limits<uint64_t>::max();
        }
        else
        {
            route.reportedDistance = calculateCompositeMetric(route.load, route.reliability, route.delay, route.bandwidth);
            route.feasibleDistance = route.reportedDistance + local;
        }
    }
}

uint64_t InterfaceMetrics::getLocalMetric()
{
    auto& cfg = iface.getIfaceCfg();
    uint8_t load = cfg.load.load(std::memory_order_relaxed);
    uint8_t reliability = cfg.reliability.load(std::memory_order_relaxed);
    uint64_t bandwidth = cfg.hwInfo.bandwidth;
    uint64_t delay = cfg.delay.load(std::memory_order_relaxed) * 10'000'000;

    return iface.getMetrics().calculateCompositeMetric(load, reliability, delay, bandwidth);
}

uint64_t InterfaceMetrics::calculateCompositeMetric(uint8_t load, uint8_t reliability, uint64_t delay, uint64_t bandwidth)
{
    const auto k = iface.getBase().getGlobalConfigMgr().getKValues();

    // Convert delay from picoseconds to microseconds
    uint64_t delayMicroseconds = delay / 1'000'000ULL;

    // Scaled values
    uint64_t scaledBW = (bandwidth > 0)
        ? (10'000'000ULL * 65'536ULL) / bandwidth
        : 0ULL;

    uint64_t scaledDelay = delayMicroseconds * 65'536ULL;

    // Load term
    uint64_t loadTerm = 0;
    if (k.k2_Load != 0 && load < 256)
        loadTerm = (k.k2_Load * scaledBW) / (256ULL - load);

    // Base metric (128-bit to avoid overflow)
    unsigned __int128 base = 0;
    base += static_cast<unsigned __int128>(k.k1_Bandwidth) * scaledBW;
    base += loadTerm;
    base += static_cast<unsigned __int128>(k.k3_Delay) * scaledDelay;

    // Final metric
    uint64_t finalMetric;
    if (k.k5_MTU == 0)
    {
        finalMetric = static_cast<uint64_t>(base);
    }
    else
    {
        uint64_t denominator = k.k4_Reliability + reliability;
        if (denominator == 0)
            return std::numeric_limits<uint64_t>::max();

        unsigned __int128 tmp = base * static_cast<unsigned __int128>(k.k5_MTU);
        tmp /= denominator;
        finalMetric = static_cast<uint64_t>(tmp);
    }

    return finalMetric;
}

double InterfaceMetrics::calculateRTT(Neighbor& neighbor, std::chrono::steady_clock::time_point& sendTime)
{
    auto now = std::chrono::steady_clock::now();
    double rttSample = std::chrono::duration<double>(now - sendTime).count();

    // Validate RTT sample
    if (rttSample <= 0.0 || rttSample > 60.0)
    {
        return neighbor.srtt.load(std::memory_order_relaxed);
    }

    return rttSample;
}

void InterfaceMetrics::updateRTTEstimate(Neighbor& neighbor, std::chrono::steady_clock::time_point& sendTime)
{
    double rttSample = calculateRTT(neighbor, sendTime);

    // Update srtt and rttvar using standard algorithms
    double alpha = 1.0 / 8.0;
    double beta = 1.0 / 4.0;

    double rttvar = neighbor.rttvar.load(std::memory_order_relaxed);
    double srtt = neighbor.srtt.load(std::memory_order_relaxed);
    double rto = neighbor.rto.load(std::memory_order_relaxed);

    rttvar = (1.0 - beta) * rttvar + beta * std::abs(srtt - rttSample);
    srtt = (1.0 - alpha) * srtt + alpha * rttSample;
    rto = srtt + std::max(0.1, 4.0 * rttvar);
    rto = std::clamp(rto, 1.0, 60.0); // Bounds: 1s to 60s

    neighbor.rttvar.store(rttvar, std::memory_order_release);
    neighbor.srtt.store(srtt, std::memory_order_release);
    neighbor.rto.store(rto, std::memory_order_release);
}
}

