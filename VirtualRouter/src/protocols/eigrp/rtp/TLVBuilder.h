// TLVBuilder.h

#ifndef EIGRP_TLV_BUILDER_H
#define EIGRP_TLV_BUILDER_H

#include <cstdint>
#include <EigrpTypes.hpp>
#include <TLVOptions.hpp>

struct IPAddress;
struct EigrpHeader;

namespace Eigrp
{
class EigrpInterface;
struct ReceivedRoute;

constexpr uint16_t ROUTE_EXTERNAL = 0b0000000000000001;
constexpr uint16_t ROUTE_WIDE_V4  = 0b0000000100000000;
constexpr uint16_t ROUTE_WIDE_V6  = 0b0000001000000000;
constexpr uint16_t ROUTE_V6       = 0b0000010000000000;

class TLVBuilder
{
public:
    using RouteVersion = uint8_t;

    TLVBuilder() = delete;
    TLVBuilder(const TLVBuilder&) = delete;
    TLVBuilder& operator=(const TLVBuilder&) = delete;

    enum class RouteType : uint16_t
    {
        LEGACY_INTERNAL     = 0x0002,
        LEGACY_EXTERNAL     = 0x0003,
        WIDE_INTERNAL       = 0x0102,
        WIDE_EXTERNAL       = 0x0103,
        LEGACY_INTERNAL_V6  = 0x0402,
        LEGACY_EXTERNAL_V6  = 0x0403,
        WIDE_INTERNAL_V6    = 0x0602,
        WIDE_EXTERNAL_V6    = 0x0603
    };

    [[nodiscard]] inline static bool isExternal(TLVBuilder::RouteType t) noexcept
        { return (static_cast<uint8_t>(t) & ROUTE_EXTERNAL) != 0; }
    [[nodiscard]] inline static bool isWideV4(TLVBuilder::RouteType t) noexcept
        { return (static_cast<uint8_t>(t) & ROUTE_WIDE_V4) != 0; }
    [[nodiscard]] inline static bool isWideV6(TLVBuilder::RouteType t) noexcept
        { return (static_cast<uint8_t>(t) & ROUTE_WIDE_V6) != 0; }
    [[nodiscard]] inline static bool isV6(TLVBuilder::RouteType t) noexcept
        { return (static_cast<uint8_t>(t) & ROUTE_V6) != 0; }

    static uint8_t encodeRouteOption(uint8_t* out, size_t maxSize, const RouteInfo* route, uint64_t currentBandwidth, uint64_t currentDelay, RouteType type);
    static uint8_t* encodeStubOption(uint8_t* out, const EigrpConfigs::StubConfig& stub);
    static std::optional<ReceivedRoute> decodeRoute(const TLV16Option& routeOpt, uint32_t ifaceLearned);
    static uint8_t* calculateParameters(uint8_t* out, const EigrpConfigs::KValue& kvalue, uint16_t holdTime = 0);

private:

    struct RouteData
    {
        RouteData(ReceivedRoute& recv) : r(recv) {}
        bool v6 = false;
        uint8_t* value = nullptr;
        ReceivedRoute& r;
        size_t offset{0};
        size_t valueSize{0};
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
}

#endif // EIGRP_TLV_BUILDER_H
