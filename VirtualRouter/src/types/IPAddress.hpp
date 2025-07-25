// IPAddress.hpp

#ifndef IPADDRESS_HPP
#define IPADDRESS_HPP

#include <cstdint>
#include <cstring>
#include <AddressFamily.hpp>
#include <functional>

struct alignas(16) IPAddress
{
    union {
        uint8_t raw[16];
        uint32_t v4;
        __uint128_t v6;
    };
    bool isV6;

    IPAddress() { v6 = 0; }

    IPAddress(const IPAddress& other)
    {
        std::memcpy(raw, other.raw, 16);
        isV6 = other.isV6;
    }

    IPAddress& operator=(const IPAddress& other)
    {
        if (this != &other) {
            std::memcpy(raw, other.raw, 16);
            isV6 = other.isV6;
        }
        return *this;
    }

    bool operator<(const IPAddress& other) const
    {
        if (isV6 != other.isV6)
            return isV6 < other.isV6;

        return isV6 ? (v6 < other.v6) : (v4 < other.v4);
    }

    IPAddress(const uint8_t* bytes, AddressFamily fam) {
        v6 = 0;
        isV6 =  (fam == AddressFamily::IPv6);
        std::memcpy(raw, bytes, fam == AddressFamily::IPv4 ? 4 : 16);
    }
    
    bool operator==(const IPAddress& other) const 
    {
        if (isV6 != other.isV6) return false;
        return isV6 ? (v6 == other.v6) : (v4 == other.v4);
    }
};

struct IPPrefix
{
    uint8_t addr[16];
    uint8_t prefixLength{};
    AddressFamily af{};
    
    IPPrefix() = default;

    IPPrefix(const uint8_t* ip, uint8_t prefix, AddressFamily family)
        : prefixLength(prefix), af(family) {
        std::memset(addr, 0, 16);
        std::memcpy(addr, ip, (af == AddressFamily::IPv4) ? 4 : 16);
    }

    bool operator==(const IPPrefix& other) const {
        if (af != other.af || prefixLength != other.prefixLength) return false;
        size_t len = (af == AddressFamily::IPv4) ? 4 : 16;
        return std::memcmp(addr, other.addr, len) == 0;
    }

    bool operator<(const IPPrefix& other) const {
        if (af != other.af) return af < other.af;
        if (prefixLength != other.prefixLength) return prefixLength < other.prefixLength;
        size_t len = (af == AddressFamily::IPv4) ? 4 : 16;
        return std::memcmp(addr, other.addr, len) < 0;
    }
};

struct IPv6Prefix
{
    __uint128_t addr;
    uint8_t prefixLength{};
    
    IPv6Prefix() = default;

    IPv6Prefix(__uint128_t ip, uint8_t prefix)
        : addr(ip), prefixLength(prefix) {
    }

    bool operator==(const IPv6Prefix& other) const {
        return other.prefixLength == prefixLength && other.addr == addr;
    }

    bool operator<(const IPv6Prefix& other) const {
        if (prefixLength != other.prefixLength) return prefixLength < other.prefixLength;
        return addr < other.addr;
    }
};

namespace std {

template <>
struct hash<IPAddress> {
    size_t operator()(const IPAddress& addr) const {
        if (addr.isV6) {
            uint64_t high = static_cast<uint64_t>(addr.v6 >> 64);
            uint64_t low = static_cast<uint64_t>(addr.v6);
            high ^= low;
            high ^= high >> 33;
            high *= 0xff51afd7ed558ccdULL;
            high ^= high >> 33;
            high *= 0xc4ceb9fe1a85ec53ULL;
            high ^= high >> 33;
            return static_cast<size_t>(high);
        }
        else
        {
            uint32_t x = addr.v4;
            x ^= x >> 16;
            x *= 0x85ebca6b;
            x ^= x >> 13;
            x *= 0xc2b2ae35;
            x ^= x >> 16;
            return static_cast<size_t>(x);
        }
    }
};

template <>
struct hash<IPPrefix> {
    std::size_t operator()(const IPPrefix& key) const {
        size_t len = (key.af == AddressFamily::IPv4) ? 4 : 16;
        std::size_t h = std::hash<uint8_t>{}(static_cast<uint8_t>(key.af));
        h ^= std::hash<uint8_t>{}(key.prefixLength) + 0x9e3779b9 + (h << 6) + (h >> 2);
        for (size_t i = 0; i < len; ++i) {
            h ^= std::hash<uint8_t>{}(key.addr[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

template <>
struct hash<IPv6Prefix> {
    size_t operator()(const IPv6Prefix& pfx) const noexcept {
        // Split the __uint128_t into two 64-bit halves
        uint64_t high = static_cast<uint64_t>(pfx.addr >> 64);
        uint64_t low  = static_cast<uint64_t>(pfx.addr);

        size_t h1 = std::hash<uint64_t>{}(high);
        size_t h2 = std::hash<uint64_t>{}(low);
        size_t h3 = std::hash<uint8_t>{}(pfx.prefixLength);

        // Combine using standard hash combining formula
        size_t result = h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        result ^= h3 + 0x9e3779b9 + (result << 6) + (result >> 2);
        return result;
    }
};

}

#endif
