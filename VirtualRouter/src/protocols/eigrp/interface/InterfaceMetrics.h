// InterfaceMetrics.h

#ifndef EIGRP_INTERFACE_METRICS_H
#define EIGRP_INTERFACE_METRICS_H

#include <cstdint>
#include <chrono>

enum class AddressFamily : uint8_t;

namespace EIGRP
{
class Neighbor;
class EigrpInterface;
struct ReceivedRoute;

class InterfaceMetrics
{
public:
    InterfaceMetrics(EigrpInterface& iface);

    void addRouteMetrics(std::vector<ReceivedRoute>& routes);
    uint64_t calculateCompositeMetric(uint8_t load, uint8_t reliability, uint64_t delay, uint64_t bandwidth);
    uint64_t getLocalMetric();

    /**
     * @brief Calculates the Round-Trip Time (RTT) for a packet.
     *
     * Measures the time taken for a packet to be sent and acknowledged, updating RTT estimates
     * to inform retransmission timeouts and network performance metrics.
     * 
     * @param neighbor Neighbor.
     * @param sequenceNumber Sequence number of the packet.
     * @return Calculated RTT in seconds.
     */
    double calculateRTT(Neighbor& neighbor, std::chrono::steady_clock::time_point& sendTime, uint32_t sequenceNumber);

    /**
     * @brief Updates RTT estimates based on received ACKs.
     *
     * Refines the RTT and RTT variance calculations upon receiving an ACK, allowing for
     * more accurate retransmission timeouts and improved protocol responsiveness.
     *
     * @param neighbor Neighbor.
     * @param sequenceNumber Sequence number of the acknowledged packet.
     */
    void updateRTTEstimate(Neighbor& neighbor, std::chrono::steady_clock::time_point& sendTime, uint32_t sequenceNumber);

private:
    EigrpInterface& iface;
};
}

#endif // EIGRP_INTERFACE_METRICS_H
