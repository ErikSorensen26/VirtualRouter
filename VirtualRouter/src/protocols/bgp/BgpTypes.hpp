// BgpTypes.hpp

#ifndef BGP_TYPES_HPP
#define BGP_TYPES_HPP

#include <chrono>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include <IPAddress.hpp>

#include "packet/headers/BgpHeader.hpp"

namespace BGP
{
constexpr uint16_t kMinMessageLen = BgpHeader::fixedSize;
constexpr uint16_t kMaxMessageLen = 4096;
constexpr uint16_t kExtendedMessageLen = 65535;
constexpr uint32_t kAsTrans = 23456;

enum class FsmState : uint8_t
{
    IDLE,
    OPEN_SENT,
    OPEN_CONFIRMED,
    ESTABLISHED,
};

enum class FsmEvent : uint8_t
{
    ADMIN_START,
    ADMIN_STOP,
    TCP_UP,
    TCP_DOWN,
    RX_OPEN,
    RX_KEEPALIVE,
    RX_UPDATE,
    RX_NOTIFICATION,
    RX_ROUTE_REFRESH,
    HOLD_TIMER_EXPIRED,
    KEEPALIVE_TIMER_FIRE
};

struct Notification
{
    uint16_t code = 0;
    std::vector<uint8_t> data = {};
};

struct AfiSafi
{
    uint16_t afi = 0;
    uint8_t safi = 0;

    bool operator==(const AfiSafi& other) const noexcept
    {
        return afi == other.afi && safi == other.safi;
    }

    bool operator!=(const AfiSafi& other) const noexcept
    {
        return !(*this == other);
    }
};

inline AddressFamily toAddressFamily(const AfiSafi& family) noexcept
{
    if (family.afi == BGP_AFI_IPV4)
        return AddressFamily::IPv4;
    if (family.afi == BGP_AFI_IPV6)
        return AddressFamily::IPv6;
    return AddressFamily::NONE;
}
}

namespace std
{
template <>
struct hash<BGP::AfiSafi>
{
    size_t operator()(const BGP::AfiSafi& family) const noexcept
    {
        uint32_t v = (static_cast<uint32_t>(family.afi) << 8) | family.safi;
        return std::hash<uint32_t>{}(v);
    }
};
}

#endif // BGP_TYPES_HPP
