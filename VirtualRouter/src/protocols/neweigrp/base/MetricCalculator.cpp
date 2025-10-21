// EigrpMetrics.cpp

#include "EigrpMetrics.h"

namespace Protocol
{
uint64_t Eigrp::calculateCompositeMetric(uint32_t bandwidth, uint8_t load, uint32_t delay, uint8_t reliability, uint8_t hopCount)
{
    if (bandwidth == 0) return std::numeric_limits<uint32_t>::max();

    EigrpConfigs::KValue kvalue;
    {
        std::shared_lock<std::shared_mutex> configMutex(configs.configsMutex);
        kvalue = configs.kvalue;
    }

    // Calculate individual components of the metric
    uint64_t bandwidthMetric = configs.wideMetric.load(std::memory_order_relaxed) / bandwidth;
    uint64_t delayMetric = delay;
    uint64_t loadMetric = (kvalue.k2_Load * bandwidthMetric) / (256 - load);

    uint64_t compositeMetric = (kvalue.k1_Bandwidth * bandwidthMetric) +
                             loadMetric +
                             (kvalue.k3_Delay * delayMetric);

    // Account for K5 (optional scaling)
    if (kvalue.k5_MTU != 0 && (reliability + kvalue.k4_Reliability) > 0) {
        compositeMetric *= kvalue.k5_MTU / (reliability + kvalue.k4_Reliability);
    }

    // Final scaling for the metric
    compositeMetric = std::min(compositeMetric, 16777215UL);
    return compositeMetric * 256; // Max metric value
}

uint32_t Eigrp::calculateLocalCost(uint8_t load, uint32_t delay, uint8_t reliability)
{
    uint32_t bandwidth;
    EigrpConfigs::KValue kvalue;
    {
        std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
        kvalue = configs.kvalue;
    }
    bandwidth = configs.lowestBandwidth.load(std::memory_order_relaxed);

    uint32_t bandwidthMetric = (configs.wideMetric.load(std::memory_order_relaxed)) / bandwidth;
    uint32_t delayMetric = delay / 10;

    uint32_t loadMetric = 0;
    if (kvalue.k2_Load != 0 && (256.0 - load) != 0)
    {
        loadMetric = (kvalue.k2_Load * load) / (256 - load);
    }

    // Calculate link cost using K-values
    uint32_t linkCost = (kvalue.k1_Bandwidth * bandwidthMetric) +
                      loadMetric +
                      (kvalue.k3_Delay * delayMetric);

    // Apply scaling factor and reliability
    uint32_t reliabilitySum = reliability + kvalue.k4_Reliability;
    if (reliabilitySum > 0 && kvalue.k5_MTU != 0)
    {
        linkCost *= (kvalue.k5_MTU) / reliabilitySum;
    }

    return linkCost * 256;
}

uint32_t Eigrp::getLowestBandwidth()
{
    uint32_t lowestBW = std::numeric_limits<uint32_t>::max();
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    for (const auto& [_, eigrpInterfacePtr] : eigrpInterfaceList)
    {
        if (!eigrpInterfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed) && eigrpInterfacePtr->currentInterfaceInfo->bandwidth < lowestBW)
        {
            lowestBW = eigrpInterfacePtr->currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed);
        }
    };
    return lowestBW;
}

void Eigrp::setVariance(uint8_t var)
{
    if ( var == 0 ) return; // Invalid variance
    for (auto routeInfo : topologyTable->getTopologyEntries())
    {
        topologyTable->updateSuccessorAndFeasibleSuccessors(routeInfo.second);
    }
    std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
    configs.variance = var;
}
}
