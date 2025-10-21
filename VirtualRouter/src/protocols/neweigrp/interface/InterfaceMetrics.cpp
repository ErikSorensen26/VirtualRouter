// EigrpInterfaceMetrics.cpp

#include "EigrpInterfaceMetrics.h"

namespace Protocol
{
double EigrpInterface::calculateRTT(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber)
{
    auto sendTimeIt = neighbor->reliablePackets.find(sequenceNumber);

    if (sendTimeIt != neighbor->reliablePackets.end())
    {
        auto sendTime = sendTimeIt->second.sendTime;
        auto now = std::chrono::steady_clock::now();
        double rttSample = std::chrono::duration<double>(now - sendTime).count();

        // Validate RTT sample
        if (rttSample <= 0.0 || rttSample > 60.0)
        {
            return neighbor->srtt;
        }

        return rttSample;
    }
    return neighbor->srtt;
}

void EigrpInterface::updateRTTEstimate(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber)
{
    double rttSample = calculateRTT(neighbor, sequenceNumber);

    // Update srtt and rttvar using standard algorithms
    double alpha = 1.0 / 8.0;
    double beta = 1.0 / 4.0;

    {
        std::unique_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
        neighbor->rttvar = (1.0 - beta) * neighbor->rttvar + beta * std::abs(neighbor->srtt - rttSample);
        neighbor->srtt = (1.0 - alpha) * neighbor->srtt + alpha * rttSample;
        neighbor->rto = neighbor->srtt + std::max(0.1, 4.0 * neighbor->rttvar);
        neighbor->rto = std::clamp(neighbor->rto, 1.0, 60.0); // Bounds: 1s to 60s
    }
}

bool EigrpInterface::isTimeoutForMissing(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber)
{
    auto now = std::chrono::steady_clock::now();
    if (neighbor->missingPacketTimestamps.count(sequenceNumber) == 0)
    {
        // First time seeing the missing packet
        neighbor->missingPacketTimestamps[sequenceNumber] = now;
        return false;
    }

    // Check if the timeout has been exceeded
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - neighbor->missingPacketTimestamps[sequenceNumber]).count();
    return elapsed > PACKET_TIMEOUT_MS;
}

size_t EigrpInterface::calculateMaxRoutesPerPacket(size_t baseSize, AddressFamily af, bool isExternal)
{
    uint16_t maxPacketSize = eigrpProcess.addressFamily == AddressFamily::IPv4 
        ? currentInterfaceInfo->ipv4.mtu.load(std::memory_order_relaxed)
        : currentInterfaceInfo->ipv6.mtu.load(std::memory_order_relaxed);
    size_t headerSize = baseSize; // Estimate size of EIGRP header and base overhead
    size_t routeSize = 0;

    if (af == AddressFamily::IPv4)
    {
        routeSize = 20; // Base size for IPv4 route
        if (isExternal)
        {
            routeSize += 20; // Additional size for external routes
        }
    }
    else if (af == AddressFamily::IPv6)
    {
        routeSize = 40; // Base size for IPv6 route
        if (isExternal)
        {
            routeSize += 20; // Additional size for external routes
        }
    }

    return (maxPacketSize - headerSize) / routeSize;
}
}

