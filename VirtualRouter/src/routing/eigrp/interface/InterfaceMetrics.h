/**
 * @file InterfaceMetrics.h
 * @brief EIGRP composite metric computation for a single interface.
 */

#ifndef EIGRP_INTERFACE_METRICS_H
#define EIGRP_INTERFACE_METRICS_H

#include <cstdint>
#include <chrono>

namespace routing::eigrp
{
class Neighbor;
class EigrpInterface;
struct ReceivedRoute;

/**
 * @brief Computes EIGRP composite metrics for an interface and its learned
 *        routes.
 *
 * The composite metric formula uses the configured K-values together with
 * bandwidth, delay, load, and reliability to produce a 64-bit metric value.
 * @c InterfaceMetrics also maintains a smoothed round-trip time (SRTT)
 * estimate per neighbor for use by the Reliable Transport Protocol.
 *
 * @ingroup EIGRP_INTERFACE
 */
class InterfaceMetrics
{
public:
    /**
     * @brief Constructs an InterfaceMetrics bound to the given interface.
     * @param iface The owning EigrpInterface.
     */
    InterfaceMetrics(EigrpInterface& iface);

    /**
     * @brief Augments a list of received routes with the local interface
     *        metric contribution (adds local bandwidth and delay).
     * @param routes Routes decoded from an incoming Update packet whose
     *               metric fields will be updated in-place.
     */
    void addRouteMetrics(std::vector<ReceivedRoute>& routes);

    /**
     * @brief Computes the EIGRP composite metric from raw interface parameters.
     * @param load        Current interface load (0–255).
     * @param reliability Current interface reliability (0–255).
     * @param delay       Accumulated path delay in picoseconds.
     * @param bandwidth   Minimum path bandwidth in kilobits per second.
     * @return 64-bit composite metric value.
     */
    uint64_t calculateCompositeMetric(uint8_t load, uint8_t reliability, uint64_t delay, uint64_t bandwidth);

    /**
     * @brief Returns the current composite metric for the local interface
     *        (bandwidth + delay only, using configured K-values).
     * @return 64-bit local interface metric.
     */
    uint64_t getLocalMetric();

    /**
     * @brief Computes the instantaneous round-trip time for a neighbor based
     *        on when a packet was sent.
     * @param neighbor  The neighbor whose RTT is being measured.
     * @param sendTime  The time the packet was originally sent.
     * @return RTT in seconds as a floating-point value.
     */
    double calculateRTT(Neighbor& neighbor, std::chrono::steady_clock::time_point& sendTime);

    /**
     * @brief Updates the smoothed RTT (SRTT) and retransmission timeout (RTO)
     *        estimate for a neighbor using the RFC 6298 algorithm.
     * @param neighbor  The neighbor whose SRTT/RTO values are updated.
     * @param sendTime  The time the acknowledged packet was originally sent.
     */
    void updateRTTEstimate(Neighbor& neighbor, std::chrono::steady_clock::time_point& sendTime);

private:
    EigrpInterface& iface; ///< The interface whose K-values and link parameters are used.
};
} // namespace routing

#endif // EIGRP_INTERFACE_METRICS_H

