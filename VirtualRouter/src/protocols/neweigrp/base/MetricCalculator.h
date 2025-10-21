// EigrpMetrics.h

#ifndef EIGRP_METRICS_H
#define EIGRP_METRICS_H

#include <cstdint>

namespace Protocol
{
class EigrpMetrics
{
public:

    /**
     * @brief Calculates the EIGRP metric for a route.
     *
     * Computes the EIGRP metric based on the provided parameters, adhering to
     * EIGRP's metric calculation formula which considers bandwidth, load, delay,
     * reliability, and optionally hop count for unequal-cost load balancing.
     *
     * @param bandwidth Bandwidth value in Kbps.
     * @param load Load value (0-255).
     * @param delay Delay value in tens of microseconds.
     * @param reliability Reliability value (0-255).
     * @param hopCount Number of hops (default is 0).
     * @return Calculated metric value.
     */
    uint64_t calculateCompositeMetric(uint32_t bandwidth, uint8_t load, uint32_t delay, uint8_t reliability, uint8_t hopCount = 0);

    /**
     * @brief Calculates the Local Link Cost (LLC) for the interface.
     *
     * Computes the LLC based on interface metrics such as bandwidth, delay, reliability,
     * and load, contributing to the overall EIGRP metric calculation.
     *
     * @param load Load of the interface used.
     * @param delay Delay of the interface used.
     * @return Calculated LLC value.
     */
    uint32_t calculateLocalCost(uint8_t load, uint32_t delay, uint8_t reliability);

    /**
     * @brief Retrieves the lowest bandwidth among all configured interfaces.
     *
     * Scans all EIGRP-configured interfaces to determine the minimum bandwidth value,
     * which is crucial for metric calculations and route selection processes.
     *
     * @return Lowest bandwidth value in Kbps.
     */
    uint32_t getLowestBandwidth();

    /**
     * @brief Sets the variance for unequal-cost load balancing.
     *
     * Configures the variance multiplier, allowing EIGRP to utilize multiple routes
     * with feasible distances within the specified variance factor, enabling unequal-cost
     * load balancing across multiple paths.
     *
     * @param var Variance value.
     */
    void setVariance(uint8_t var);

    void refreshInterfaceMetrics();
};
}

#endif // EIGRP_METRICS_H
