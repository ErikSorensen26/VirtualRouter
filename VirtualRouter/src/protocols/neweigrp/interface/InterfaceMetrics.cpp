// EigrpInterfaceMetrics.cpp

#include "InterfaceMetrics.h"
#include "EigrpInterface.h"
#include "InterfaceConfigs.h"
#include <HardwareManager.h>
#include <EigrpCore.h>
#include <EigrpConfig.h>
#include <Neighbor.h>

namespace Eigrp
{
InterfaceMetrics::InterfaceMetrics(EigrpInterface& iface) : iface(iface) {}

void InterfaceMetrics::addRouteMetrics(std::vector<ReceivedRoute>& routes)
{
    uint64_t local = getLocalMetric();
    for (auto& route : routes)
    {
        route.reportedDistance = calculateCompositeMetric(route.load, route.reliability, route.delay, route.bandwidth);
        if (local != 0)
            route.feasibleDistance = route.reportedDistance + local;
        else
            route.feasibleDistance = route.reportedDistance + getLocalMetric();
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
    EigrpConfigs::KValue k = iface.getBase().getGlobalConfigMgr().getKValues();

    uint64_t scaledBW = (bandwidth > 0) ? (10'000'000ull * 65'536ull) / bandwidth : 0ull;
    uint64_t scaledDelay = (delay * 65'536ull) / 1'000'000ull;

    uint64_t loadMetric = 0ull;
    if (k.k2_Load != 0)
    {
        if (load < 255)
            loadMetric = (k.k2_Load * scaledBW) / (256ull - load);
        else if (k.k2_Load != 0 && load >= 255)
            loadMetric = k.k2_Load * scaledBW;
    }

    // Calculate link cost using K-values
    uint64_t composite = (k.k1_Bandwidth * scaledBW) +
                      loadMetric +
                      (k.k3_Delay * scaledDelay);

    // Apply scaling factor and reliability
    if (k.k5_MTU != 0)
    {
        uint64_t denom = reliability + k.k4_Reliability;
        if (denom > 0)
            composite = (composite * k.k5_MTU) / denom;
    }

    return composite;
}

double InterfaceMetrics::calculateRTT(Neighbor& neighbor, std::chrono::steady_clock::time_point& sendTime, uint32_t seq)
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

void InterfaceMetrics::updateRTTEstimate(Neighbor& neighbor, std::chrono::steady_clock::time_point& sendTime, uint32_t seq)
{
    double rttSample = calculateRTT(neighbor, sendTime, seq);

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

