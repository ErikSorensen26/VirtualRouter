// EigrpInterfaceMetrics.h

#ifndef EIGRP_INTERFACE_METRICS_H
#define EIGRP_INTERFACE_METRICS_H

#include <cstdint>
#include <cstddef>

enum class AddressFamily : uint8_t;
namespace EigrpConfigs
{
struct NeighborInfo;
}

namespace Protocol
{
class EigrpInterfaceMetrics
{
public:

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
    double calculateRTT(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber);

    /**
     * @brief Updates RTT estimates based on received ACKs.
     *
     * Refines the RTT and RTT variance calculations upon receiving an ACK, allowing for
     * more accurate retransmission timeouts and improved protocol responsiveness.
     *
     * @param neighbor Neighbor.
     * @param sequenceNumber Sequence number of the acknowledged packet.
     */
    void updateRTTEstimate(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber);

    /**
     * @brief Checks if a timeout has occurred for missing packets.
     *
     * Determines whether a retransmission timeout has been reached for a specific packet.
     * If a timeout is detected, appropriate actions such as retransmission or neighbor
     * down status updates are triggered.
     *
     * @param neighbor Pointer to the neighbor's information.
     * @param sequenceNumber Sequence number of the packet.
     * @return True if a timeout has occurred, false otherwise.
     */
    bool isTimeoutForMissing(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber);

    /**
     * @brief Calculates the maximum number of routes to be sent in a single Update packet.
     *
     * Determines the upper limit on the number of routing entries that can be included
     * in a single Update packet based on the address family and whether the routes are
     * external.
     *
     * @param af Address family (IPv4/IPv6).
     * @param isExternal Indicates if the route is external.
     * @return Maximum number of routes per packet.
     */
    size_t calculateMaxRoutesPerPacket(size_t baseSize, AddressFamily af, bool isExernal);
};
}

#endif // EIGRP_INTERFACE_MANAGER_H
