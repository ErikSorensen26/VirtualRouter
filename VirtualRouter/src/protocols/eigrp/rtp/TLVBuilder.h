// TLVBuilder.h

#ifndef EIGRP_TLV_BUILDER_H
#define EIGRP_TLV_BUILDER_H

#include <cstdint>
#include <EigrpTypes.hpp>
#include <Neighbor.h>
#include <TlvOptions.hpp>

struct IPAddress;
struct EigrpHeader;

namespace Eigrp
{
class EigrpInterface;
struct ReceivedRoute;

class TLVBuilder
{
public:
    using RouteVersion = uint8_t;

    TLVBuilder() = delete;
    TLVBuilder(const TLVBuilder&) = delete;
    TLVBuilder& operator=(const TLVBuilder&) = delete;

    enum class RouteType : uint16_t
    {
        LEGACY_INTERNAL     = 0x0102,
        LEGACY_EXTERNAL     = 0x0103,
        LEGACY_INTERNAL_V6  = 0x0402,
        LEGACY_EXTERNAL_V6  = 0x0403,
        WIDE_INTERNAL    = 0x0602,
        WIDE_EXTERNAL    = 0x0603
    };

    [[nodiscard]] inline static bool isExternal(TLVBuilder::RouteType t) noexcept
        { return (static_cast<uint16_t>(t) & 0x00FF) == 0x0003; }
    [[nodiscard]] inline static bool isWide(TLVBuilder::RouteType t) noexcept
        { return (static_cast<uint16_t>(t) & 0xFF00) != 0x0100; }
    [[nodiscard]] inline static bool isNamed(TLVBuilder::RouteType t) noexcept
        { return (static_cast<uint16_t>(t) & 0xFF00) == 0x0600; }

    static uint8_t encodeRouteOption(EigrpInterface& iface, uint8_t* out, size_t maxSize, const RouteInfo* route, uint64_t currentBandwidth, uint64_t currentDelay, RouteType type);
    static uint8_t* encodeStubOption(uint8_t* out, const EigrpConfigs::StubConfig& stub);
    static std::optional<ReceivedRoute> decodeRoute(const TLV16Option& routeOpt, uint32_t ifaceLearned, AddressFamily af);
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
