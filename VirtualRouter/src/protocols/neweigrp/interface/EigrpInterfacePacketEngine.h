// EigrpInterfacePacketEngine.h

#ifndef HEADER_ENGINE_H
#define HEADER_ENGINE_H

#include <cstdint>
#include <vector>
#include <EigrpTypes.hpp>

struct IPAddress;
struct EigrpHeader;

namespace Protocol
{
class EigrpInterfacePacketEngine
{
public:

private:

public:

    /**
     * @brief Encodes a Route option for a specific route.
     *
     * Serializes the Route option, including route metrics and other relevant data,
     * into a ByteString for transmission.
     *
     * @param route Route information.
     * @param currentBandwidth Bandwidth of the current interface.
     * @param currentDelay Delay of the current Interface.
     * @param removed Indicates if the route is being removed.
     * @return Encoded Route option as ByteString.
     */
    uint8_t encodeRouteOption(uint8_t* out, RoutingTable::Eigrp* route, uint32_t currentBandwidth, uint32_t currentDelay, bool removed = false);

    /**
     * @brief Encodes an External Route option for a specific route.
     *
     * Serializes the External Route option, which includes additional metrics for external
     * routes, into a ByteString for inclusion in an EIGRP packet.
     *
     * @param route External route information.
     * @param currentBandwidth Bandwidth of the current interface.
     * @param currentDelay Delay of the current Interface.
     * @param removed Indicates if the route is being removed.
     * @return Encoded External Route option as ByteString.
     */
    uint8_t encodeExternalRouteOption(uint8_t* out, RoutingTable::Eigrp* route, uint32_t currentBandwidth, uint32_t currentDelay, bool removed = false);

    /**
     * @brief Encodes a Stub option based on stub configuration.
     *
     * Constructs the Stub option TLV (Type-Length-Value) based on the provided
     * stub configuration settings.
     *
     * @param stub Stub configuration.
     * @return Encoded Stub option as ByteString.
     */
    uint8_t* encodeStubOption(uint8_t* out, const EigrpConfigs::StubConfig& stub);

    /**
     * @brief Encodes a Summary Route for advertisement.
     *
     * Serializes the Summary Route into a format suitable for inclusion in an EIGRP
     * Update packet, consolidating route information for efficient transmission.
     *
     * @param summaryRoute Summary route information.
     * @return Encoded Summary Route as RoutingTable::Eigrp.
     */
    RoutingTable::Eigrp* encodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);

    /**
     * @brief Decodes a Route from a ByteString.
     *
     * Deserializes the Route option from its ByteString representation into a RoutingTable::Eigrp
     * structure, extracting all relevant routing metrics and information.
     *
     * @param value ByteString containing the encoded route.
     * @param external Indicates if the route is external.
     * @param summary Indicates if the route is a summary route.
     * @return Decoded EIGRP route.
     */
    RoutingTable::Eigrp* decodeRoute(const uint8_t* value, size_t valueSize, bool external, bool summary);

    /**
     * @brief Calculates EIGRP parameters based on the hold time.
     *
     * Derives EIGRP parameter values such as retransmission timeout and others
     * based on the configured hold time, ensuring synchronization with neighbor
     * timers and state management.
     *
     * @param holdTime Hold time in seconds.
     * @return ByteString representing calculated parameters.
     */
    uint8_t* calculateParameters(uint8_t* out, uint16_t holdTime);

    // Route Buffer
    std::vector<EigrpConfigs::RoutingUpdate> routeBuffer; ///< Buffer for routing updates.
    std::mutex bufferMutex;
};
}

#endif // HEADER_ENGINE_H
