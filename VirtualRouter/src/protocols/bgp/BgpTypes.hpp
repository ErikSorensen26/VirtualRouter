// BgpTypes.hpp

#ifndef BGP_TYPES_HPP
#define BGP_TYPES_HPP

#include <cstdint>
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

struct NeighborKey
{
    IPAddress ipAddr;
    uint16_t afi;
    uint8_t safi;

    bool operator==(const NeighborKey& other) const noexcept
    {
        return ipAddr == other.ipAddr &&
               afi == other.afi &&
               safi == other.safi;
    }
};

struct PeerKey
{
    PeerKey() = default;
    PeerKey(const NeighborKey& key, uint32_t rid)
        : rid(rid), afi(key.afi), safi(key.safi) {}

    uint32_t rid;
    uint16_t afi;
    uint8_t safi;

    bool operator==(const PeerKey& other) const noexcept
    {
        return rid == other.rid &&
               afi == other.afi &&
               safi == other.safi;
    }
};

inline AddressFamily toAddressFamily(const AfiSafi& family) noexcept {
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

template <>
struct hash<BGP::NeighborKey>
{
    size_t operator()(const BGP::NeighborKey& k) const noexcept
    {
        size_t h1 = std::hash<IPAddress>{}(k.ipAddr);
        size_t h2 = std::hash<uint32_t>{}((static_cast<uint32_t>(k.afi) << 8) | k.safi);

        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

template <>
struct hash<BGP::PeerKey>
{
    size_t operator()(const BGP::PeerKey& k) const noexcept
    {
        size_t h1 = std::hash<uint32_t>{}(k.rid);
        size_t h2 = std::hash<uint32_t>{}((static_cast<uint32_t>(k.afi) << 8) | k.safi);

        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};
}

#endif // BGP_TYPES_HPP
