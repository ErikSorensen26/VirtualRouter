// Dhcpv6.h

#ifndef DHCPV6_H
#define DHCPV6_H

#include <DhcpInfo.hpp>

enum class Dhcpv6StatusCode : uint16_t
{
    Success                     = 0,
    UnspecFail                  = 1,
    NoAddrsAvail                = 2,
    NoBinding                   = 3,
    NotOnLink                   = 4,
    UseMulticast                = 5,
    NoPrefixAvail               = 6,
    UnknownQueryType            = 7,
    MalformedQuery              = 8,
    NotConfigured               = 9,
    NotAllowed                  = 10,
    QueryTerminated             = 11,
    DataMissing                 = 12,
    CatchUpComplete             = 13,
    NotSupported                = 14,
    None
};

enum class IAType : uint8_t
{
    IA_NA,
    IA_TA
};

struct Dhcpv6StatusMessage
{
    Dhcpv6StatusCode code = Dhcpv6StatusCode::None;
    std::string msg;
};

struct IAKey
{
    ClientID duid;
    uint32_t iaid;
    IAType type;

    bool operator==(const IAKey& o) const { return duid == o.duid && iaid == o.iaid && type == o.type; }
};

struct IALeaseKey
{
    ClientID duid;
    uint32_t iaid;
    __uint128_t address;
    IAType type;

    bool operator==(const IALeaseKey& o) const {
        return duid == o.duid && iaid == o.iaid && address == o.address && type == o.type;
    }
};

struct IAPrefixKey
{

    ClientID duid;
    uint32_t iaid;
    __uint128_t address;
    uint8_t prefixLength;

    bool operator==(const IAPrefixKey& o) const {
        return duid == o.duid && iaid == o.iaid &&
            address == o.address && prefixLength == o.prefixLength;
    }
};

namespace std
{
    template<>
    struct hash<__uint128_t> {
        size_t operator()(const __uint128_t& k) const {
            uint64_t high = static_cast<uint64_t>(k >> 64);
            uint64_t low = static_cast<uint64_t>(k);
            size_t seed = std::hash<uint64_t>{}(low);
            seed ^= std::hash<uint64_t>{}(high) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
    template<>
    struct hash<IAKey> {
        size_t operator()(const IAKey& k) const {
            size_t h1 = std::hash<ClientID>{}(k.duid);
            size_t h2 = std::hash<uint32_t>{}(k.iaid);
            size_t h3 = std::hash<uint8_t>{}(static_cast<uint8_t>(k.type));

            size_t seed = h1;
            seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
    template<>
    struct hash<IALeaseKey> {
        size_t operator()(const IALeaseKey& k) const {
            size_t h1 = std::hash<ClientID>{}(k.duid);
            size_t h2 = std::hash<uint32_t>{}(k.iaid);
            size_t h3 = std::hash<__uint128_t>{}(k.address);
            size_t h4 = std::hash<uint8_t>{}(static_cast<uint8_t>(k.type));

            size_t seed = h1;
            seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h4 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
    template<>
    struct hash<IAPrefixKey> {
        size_t operator()(const IAPrefixKey& k) const {
            size_t h1 = std::hash<ClientID>{}(k.duid);
            size_t h2 = std::hash<uint32_t>{}(k.iaid);
            size_t h3 = std::hash<__uint128_t>{}(k.address);
            size_t h4 = std::hash<uint8_t>{}(k.prefixLength);

            size_t seed = h1;
            seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h4 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
}

#endif // DHCP_H
