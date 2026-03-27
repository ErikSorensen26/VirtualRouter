/**
 * @file TLVBuilder.h
 * @brief EIGRP TLV builder: internal routes, external routes, parameters.
 */

#ifndef EIGRP_TLV_BUILDER_H
#define EIGRP_TLV_BUILDER_H

#include <cstdint>

#include "eigrp/EigrpTypes.hpp"
#include "eigrp/rtp/Neighbor.h"
#include "packet/TlvOptions.hpp"

namespace types { struct IPAddress; }

namespace routing::eigrp
{
class EigrpInterface;
struct ReceivedRoute;
struct RouteInfo;

/**
 * @brief Stateless encoder/decoder for all EIGRP route and parameter TLVs.
 * @ingroup EIGRP_RTP
 *
 * TLVBuilder is a pure-static utility class that handles the wire-format
 * serialization of EIGRP TLV options. It covers both classic (legacy) and
 * wide (named-mode) metric formats across IPv4 and IPv6, as well as external
 * route information and the parameter TLV carried in HELLO packets.
 *
 * ## Architectural Role
 * TLVBuilder sits between the route data model (@ref RouteInfo / @ref ReceivedRoute)
 * and the raw byte stream. @ref ReliableTransport delegates all TLV encoding to
 * this class when assembling UPDATE, QUERY, and REPLY packets, and delegates
 * decoding when a packet is received. No state is held here; every method
 * operates on caller-supplied buffers or option objects.
 *
 * ## Lifecycle & Ownership
 * Not instantiable. All methods are static. Callers own every buffer passed in
 * and every @ref ReceivedRoute returned by @ref decodeRoute.
 *
 * @warning `encodeRouteOption` writes directly into a caller-supplied output
 * buffer. The caller is responsible for ensuring `maxSize` is accurate;
 * exceeding it results in a buffer overrun.
 *
 * @see ReliableTransport
 * @see EigrpPacketBuilder
 */
class TLVBuilder
{
public:
    /// Alias used when distinguishing classic (v1) from wide (v2) metric encoding.
    using RouteVersion = uint8_t;

    TLVBuilder() = delete;
    TLVBuilder(const TLVBuilder&) = delete;
    TLVBuilder& operator=(const TLVBuilder&) = delete;

    /**
     * @brief Wire-format type codes for EIGRP route TLVs.
     * @ingroup EIGRP_RTP
     *
     * The high byte encodes the address family / format generation; the low
     * byte distinguishes internal (0x02) from external (0x03) routes.
     */
    enum class RouteType : uint16_t
    {
        LEGACY_INTERNAL     = 0x0102, ///< Classic IPv4 internal route TLV.
        LEGACY_EXTERNAL     = 0x0103, ///< Classic IPv4 external route TLV.
        LEGACY_INTERNAL_V6  = 0x0402, ///< Classic IPv6 internal route TLV.
        LEGACY_EXTERNAL_V6  = 0x0403, ///< Classic IPv6 external route TLV.
        WIDE_INTERNAL    = 0x0602,    ///< Wide-metric internal route TLV (named mode).
        WIDE_EXTERNAL    = 0x0603,    ///< Wide-metric external route TLV (named mode).
    };

    /**
     * @brief Returns true if the TLV type carries external-route information.
     *
     * External routes have a low byte of 0x03 regardless of address family or
     * metric generation.
     */
    [[nodiscard]] inline static bool isExternal(TLVBuilder::RouteType t) noexcept
        { return (static_cast<uint16_t>(t) & 0x00FF) == 0x0003; }

    /**
     * @brief Returns true if the TLV type uses wide (named-mode) metric encoding.
     *
     * Wide types have a high byte other than 0x01 (the classic IPv4 family code).
     */
    [[nodiscard]] inline static bool isWide(TLVBuilder::RouteType t) noexcept
        { return (static_cast<uint16_t>(t) & 0xFF00) != 0x0100; }

    /**
     * @brief Returns true if the TLV type is a named-mode (0x06xx) route.
     *
     * Named-mode routes always use wide metrics and carry a different on-wire
     * layout compared to classic or IPv6 legacy types.
     */
    [[nodiscard]] inline static bool isNamed(TLVBuilder::RouteType t) noexcept
        { return (static_cast<uint16_t>(t) & 0xFF00) == 0x0600; }

    /**
     * @brief Encodes a single route into the output buffer as an EIGRP route TLV.
     *
     * Selects classic or wide metric encoding based on `type`, then writes the
     * destination prefix, metric fields, and (for external routes) the
     * redistribution origin data. The encoded TLV length is written into the
     * TLV header by the callee.
     *
     * @param iface            Interface on which the route will be advertised; used
     *                         to obtain local metric contributions.
     * @param out              Pointer to the start of the output buffer region.
     * @param maxSize          Maximum bytes that may be written starting at `out`.
     * @param route            Route descriptor to encode, or nullptr for a
     *                         self-originated route with no inherited metric.
     * @param currentBandwidth Composite bandwidth metric (scaled) to encode.
     * @param currentDelay     Composite delay metric (scaled) to encode.
     * @param type             TLV type code controlling address family and format.
     * @return Number of bytes written into `out`, or 0 on encoding failure.
     *
     * @warning Writing past `maxSize` is undefined behavior. The caller must
     * pre-calculate sufficient space before invoking this method.
     */
    static uint8_t encodeRouteOption(EigrpInterface& iface, uint8_t* out, size_t maxSize, const RouteInfo* route, uint64_t currentBandwidth, uint64_t currentDelay, RouteType type);

    /**
     * @brief Encodes the STUB option TLV and advances the output pointer.
     *
     * The stub TLV advertises this router's stub capability flags to neighbors
     * so they can suppress query traffic accordingly.
     *
     * @param out  Pointer to the current write position in the packet buffer.
     * @param stub Stub configuration flags to encode.
     * @return Pointer advanced past the encoded TLV.
     */
    static uint8_t* encodeStubOption(uint8_t* out, const eigrp::StubConfig& stub);

    /**
     * @brief Decodes a raw route TLV option into a @ref ReceivedRoute.
     *
     * Parses the wire-format TLV, extracts the metric, destination prefix, and
     * any external origin data, and returns a fully populated @ref ReceivedRoute.
     * Returns `std::nullopt` if the TLV is malformed or its type is unrecognized.
     *
     * @param routeOpt     The raw TLV16 option to decode.
     * @param ifaceLearned Interface index on which the containing packet arrived.
     * @param af           Address family expected for the route prefix.
     * @return Populated route on success, or `std::nullopt` on parse failure.
     */
    static std::optional<ReceivedRoute> decodeRoute(const packet::TLV16Option& routeOpt, uint32_t ifaceLearned, types::AddressFamily af);

    /**
     * @brief Encodes the EIGRP parameter TLV into the output buffer.
     *
     * The parameter TLV carries K-values and the hold time, and is included in
     * every HELLO packet. Neighbors reject adjacency if K-values do not match.
     *
     * @param out      Pointer to the current write position in the packet buffer.
     * @param kvalue   K-value set to encode.
     * @param holdTime Hold time in seconds to advertise; 0 means use the
     *                 interface default.
     * @return Pointer advanced past the encoded TLV.
     */
    static uint8_t* calculateParameters(uint8_t* out, const eigrp::KValue& kvalue, uint16_t holdTime = 0);

private:

    /**
     * @brief Transient scratch structure threaded through encode/decode helpers.
     *
     * Groups the output pointer, size tracking, and a reference to the
     * @ref ReceivedRoute being assembled so helpers share a single context
     * without passing every field individually.
     */
    struct RouteData
    {
        RouteData(ReceivedRoute& recv) : r(recv) {}
        bool v6 = false;           ///< True when encoding/decoding an IPv6 route.
        uint8_t* value = nullptr;  ///< Pointer into the TLV value region.
        ReceivedRoute& r;
        size_t offset{0};          ///< Current read/write offset within `value`.
        size_t valueSize{0};       ///< Total byte length of the TLV value region.
    };

    static bool decodeClassicMetric(RouteData& data);
    static bool encodeClassicMetric(RouteData& data, const uint64_t& delay, const uint64_t& bw);
    static bool decodeWideMetric(RouteData& data);
    static bool encodeWideMetric(RouteData& data, const uint64_t& delay, const uint64_t& bw);
    static bool decodeExternal(RouteData& info);
    static bool encodeExternal(RouteData& info);
    static bool decodeDestination(RouteData& info);
    static bool encodeDestination(RouteData& info);
};
} // namespace routing::eigrp

#endif // EIGRP_TLV_BUILDER_H
