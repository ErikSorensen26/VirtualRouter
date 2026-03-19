// IPAddress.hpp

#ifndef IPADDRESS_HPP
#define IPADDRESS_HPP

#include <cstdint>
#include <cstring>
#include <functional>

#include "NetworkSpan.hpp"
#include "AddressFamily.hpp"
#include "packet/HeaderHelpers.hpp"

struct IPv4Address;
struct IPv6Address;
struct IPv4Prefix;
struct IPv6Prefix;
struct IPPrefix;

constexpr uint32_t v4Mask(uint8_t len)
{
    return len == 0 ? 0u : (0xFFFFFFFFu << (32 - len));
}

constexpr __uint128_t v6Mask(uint8_t len)
{
    if (len == 0)
        return 0;

    return (__uint128_t(-1)) << (128 - len);
}

struct IPAddress
{
    __uint128_t raw = 0;

    IPAddress() = default;

    IPAddress(__uint128_t addr)
        : raw(addr)
    {}

    IPAddress(__uint128_t addr, uint8_t plen)
        : raw(addr)
    {
        addPrefixLen(plen);
    }

    IPAddress(uint32_t addr)
        : raw((static_cast<__uint128_t>(addr)) | (__uint128_t{0xFFFF} << 32))
    {}

    IPAddress(uint32_t addr, uint8_t plen)
        : raw((static_cast<__uint128_t>(addr)) | (__uint128_t{0xFFFF} << 32))
    {
        addPrefixLen(plen);
    }

    IPAddress(const uint8_t* bytes, AddressFamily af)
    {
        raw = 0;
        if (af == AddressFamily::IPv4)
        {
            uint32_t ipv4 = readU32(bytes);
            raw = (static_cast<__uint128_t>(ipv4)) | (__uint128_t{0xFFFF} << 32);
        }
        else if (af == AddressFamily::IPv6)
        {
            raw = readU128(bytes);
        }
        else
        {
            throw std::runtime_error("Invalid Address Family");
        }
    }

    IPAddress(const uint8_t* bytes, AddressFamily af, uint8_t plen)
    {
        raw = 0;
        if (af == AddressFamily::IPv4)
        {
            uint32_t ipv4 = readU32(bytes);
            raw = (static_cast<__uint128_t>(ipv4)) | (__uint128_t{0xFFFF} << 32);
        }
        else if (af == AddressFamily::IPv6)
        {
            raw = readU128(bytes);
        }
        else
        {
            throw std::runtime_error("Invalid Address Family");
        }
        addPrefixLen(plen);
    }

    IPAddress(const IPPrefix& prefix);

    IPAddress(const IPAddress& other) = default;
    IPAddress(const IPAddress& other, uint8_t plen)
        : IPAddress(other.raw, plen)
    {}

    IPAddress& operator=(const IPAddress& other) = default;

    bool contains(const IPAddress& ip, uint8_t prefixLength) const
    {
        if (ip.isIPv4())
        {
            if (!isIPv4()) return false;
            uint32_t mask = v4Mask(prefixLength);
            return (static_cast<uint32_t>(v4raw()) & mask) == (ip.v4() & mask);
        }
        else
        {
            if (isIPv4()) return false;
            __uint128_t mask = v6Mask(prefixLength);
            return (static_cast<__uint128_t>(v6raw()) & mask) == (ip.v6() & mask);
        }
    }

    void addPrefixLen(uint8_t newPrefixLen)
    {
        if (isIPv4())
        {
            uint32_t v4 = static_cast<uint32_t>(v4raw());
            v4 = newPrefixLen == 0 ? 0u : v4 & (0xFFFFFFFFu << (32 - newPrefixLen));
            raw = static_cast<__uint128_t>(v4) | (__uint128_t{0xFFFF} << 32);
        }
        else
        {
            raw = newPrefixLen == 0
                ? (__uint128_t)0
                : raw & ((__uint128_t)(-1) << (128 - newPrefixLen));
        }
    }

    bool isIPv4() const noexcept { return (raw >> 32) == 0xFFFF; }
    bool isIPv6() const noexcept { return !isIPv4(); }
    bool isUnspecified() const noexcept { return raw == 0; }

    uint32_t v4() const { return static_cast<uint32_t>(raw & 0xFFFFFFFF); }
    void setV4(uint32_t ipv4) { raw = (static_cast<__uint128_t>(ipv4)) | (__uint128_t{0xFFFF} << 32); }
    NetworkSpan<uint32_t>& v4raw()
    {
        if constexpr (isLittleEndian)
            return *reinterpret_cast<NetworkSpan<uint32_t>*>(&raw);
        else
            return *reinterpret_cast<NetworkSpan<uint32_t>*>(reinterpret_cast<uint32_t*>(&raw) + 3);
    }
    const NetworkSpan<uint32_t>& v4raw() const
    {
        if constexpr (isLittleEndian)
            return *reinterpret_cast<const NetworkSpan<uint32_t>*>(&raw);
        else
            return *reinterpret_cast<const NetworkSpan<uint32_t>*>(reinterpret_cast<const uint32_t*>(&raw) + 3);
    }

    __uint128_t v6() const { return raw; }
    void setV6(__uint128_t v) { raw = v; }
    NetworkSpan<__uint128_t>& v6raw()
    {
        return *reinterpret_cast<NetworkSpan<__uint128_t>*>(&raw);
    }
    const NetworkSpan<__uint128_t>& v6raw() const
    {
        return *reinterpret_cast<const NetworkSpan<__uint128_t>*>(&raw);
    }

    bool operator==(const IPAddress& o) const { return raw == o.raw; }
    bool operator!=(const IPAddress& o) const { return raw != o.raw; }
    bool operator<(const IPAddress& o) const { return raw < o.raw; }
    bool operator>(const IPAddress& o) const { return raw > o.raw; }
    bool operator<=(const IPAddress& o) const { return raw <= o.raw; }
    bool operator>=(const IPAddress& o) const { return raw >= o.raw; }
};

struct IPv4Address
{
    uint32_t addr = 0;

    IPv4Address() = default;
    IPv4Address(uint32_t a) : addr(a) {}
    IPv4Address(uint32_t a, uint8_t plen) : addr(a) { addPrefixLen(plen); }
    IPv4Address(const uint8_t* bytes) : addr(readU32(bytes)) {}
    IPv4Address(const uint8_t* bytes, uint8_t plen) : addr(readU32(bytes)) { addPrefixLen(plen); }

    NetworkSpan<uint32_t>& raw()             { return *reinterpret_cast<NetworkSpan<uint32_t>*>(&addr); }
    const NetworkSpan<uint32_t>& raw() const { return *reinterpret_cast<const NetworkSpan<uint32_t>*>(&addr); }

    operator IPAddress() const { return IPAddress(addr); }

    bool contains(IPv4Address ip, uint8_t prefixLen)
    {
        if (prefixLen == 0) return true;
        uint32_t mask = prefixLen == 32 ? 0xFFFFFFFFu : 0xFFFFFFFFu << (32 - prefixLen);
        return (addr & mask) == (ip.addr & mask);
    }

    void addPrefixLen(uint8_t newPrefixLen)
    {
        addr = newPrefixLen == 0 ? 0u : addr & (0xFFFFFFFFu << (32 - newPrefixLen));
    }

    bool operator==(const IPv4Address& o) const { return addr == o.addr; }
    bool operator!=(const IPv4Address& o) const { return addr != o.addr; }
    bool operator< (const IPv4Address& o) const { return addr <  o.addr; }
    bool operator> (const IPv4Address& o) const { return addr >  o.addr; }
    bool operator<=(const IPv4Address& o) const { return addr <= o.addr; }
    bool operator>=(const IPv4Address& o) const { return addr >= o.addr; }

    bool operator==(uint32_t o) const { return addr == o; }
    bool operator!=(uint32_t o) const { return addr != o; }
    bool operator< (uint32_t o) const { return addr <  o; }
    bool operator> (uint32_t o) const { return addr >  o; }
    bool operator<=(uint32_t o) const { return addr <= o; }
    bool operator>=(uint32_t o) const { return addr >= o; }
};

struct IPv6Address
{
    __uint128_t addr = 0;

    IPv6Address() = default;
    IPv6Address(__uint128_t a) : addr(a) {}
    IPv6Address(__uint128_t a, uint8_t plen) : addr(a) { addPrefixLen(plen); }
    IPv6Address(const uint8_t* bytes) : addr(readU128(bytes)) {}
    IPv6Address(const uint8_t* bytes, uint8_t plen) : addr(readU128(bytes)) { addPrefixLen(plen); }

    NetworkSpan<__uint128_t>& raw()             { return *reinterpret_cast<NetworkSpan<__uint128_t>*>(&addr); }
    const NetworkSpan<__uint128_t>& raw() const { return *reinterpret_cast<const NetworkSpan<__uint128_t>*>(&addr); }

    operator IPAddress() const { return IPAddress(addr); }

    bool contains(IPv6Address ip, uint8_t prefixLen)
    {
        if (prefixLen == 0) return true;
        __uint128_t mask = prefixLen == 128 ? (__uint128_t)-1 : (__uint128_t(-1) << (128 - prefixLen));
        return (addr & mask) == (ip.addr & mask);
    }

    void addPrefixLen(uint8_t newPrefixLen)
    {
        addr = newPrefixLen == 0 ? (__uint128_t)0
            : addr & ((__uint128_t)(-1) << (128 - newPrefixLen));
    }

    bool operator==(const IPv6Address& o) const { return addr == o.addr; }
    bool operator!=(const IPv6Address& o) const { return addr != o.addr; }
    bool operator< (const IPv6Address& o) const { return addr <  o.addr; }
    bool operator> (const IPv6Address& o) const { return addr >  o.addr; }
    bool operator<=(const IPv6Address& o) const { return addr <= o.addr; }
    bool operator>=(const IPv6Address& o) const { return addr >= o.addr; }

    bool operator==(const __uint128_t& o) const { return addr == o; }
    bool operator!=(const __uint128_t& o) const { return addr != o; }
    bool operator< (const __uint128_t& o) const { return addr <  o; }
    bool operator> (const __uint128_t& o) const { return addr >  o; }
    bool operator<=(const __uint128_t& o) const { return addr <= o; }
    bool operator>=(const __uint128_t& o) const { return addr >= o; }
};

struct alignas(16) IPPrefix
{
    __uint128_t addr = 0;
    uint8_t prefixLength{};

    IPPrefix() = default;

    explicit IPPrefix(AddressFamily family)
        : addr(family == AddressFamily::IPv4 ? (__uint128_t{0xFFFF} << 32) : 0) {}

    IPPrefix(const uint8_t* ip, uint8_t prefix, AddressFamily family, bool maintainAddress = false)
    {
        if (maintainAddress)
        {
            if (family == AddressFamily::IPv4)
            {
                addr = static_cast<__uint128_t>(readU32(ip)) | (__uint128_t{0xFFFF} << 32);
                prefixLength = prefix;
            }
            else
            {
                addr = readU128(ip);
                prefixLength = prefix;
            }
        }
        else
        {
            if (family == AddressFamily::IPv4)
            {
                addr = readBytes<uint32_t>(ip, ((prefix + 7) / 8) * 8) | (__uint128_t{0xFFFF} << 32);
                addPrefixLen(prefix);
            }
            else
            {
                addr = readBytes<__uint128_t>(ip, ((prefix + 7) / 8) * 8);
                addPrefixLen(prefix);
            }
        }
    }

    IPPrefix(const IPAddress& ip, uint8_t prefix, bool maintainAddress = false)
        : addr(ip.raw)
    {
        if (maintainAddress)
            prefixLength = prefix;
        else
            addPrefixLen(prefix);
    }

    IPPrefix(IPv4Address ip, uint8_t prefix, bool maintainAddress = false)
        : addr(static_cast<__uint128_t>(ip.addr) | (__uint128_t{0xFFFF} << 32))
    {
        if (maintainAddress)
            prefixLength = prefix;
        else
            addPrefixLen(prefix);
    }

    IPPrefix(uint32_t ip, uint8_t prefix, bool maintainAddress = false)
        : addr(static_cast<__uint128_t>(ip) | (__uint128_t{0xFFFF} << 32))
    {
        if (maintainAddress)
            prefixLength = prefix;
        else
            addPrefixLen(prefix);
    }

    IPPrefix(const IPv4Prefix& prefix, bool maintainAddress = false);
    IPPrefix(const IPv6Prefix& prefix, bool maintainAddress = false);

    bool isIPv4() const noexcept { return (addr >> 32) == 0xFFFF; }
    bool isIPv6() const noexcept { return !isIPv4(); }

    uint32_t v4() const { return static_cast<uint32_t>(addr & 0xFFFFFFFF); }
    void setV4(uint32_t ipv4) { addr = (static_cast<__uint128_t>(ipv4)) | (__uint128_t{0xFFFF} << 32); }
    NetworkSpan<uint32_t>& v4raw()
    {
        if constexpr (isLittleEndian)
            return *reinterpret_cast<NetworkSpan<uint32_t>*>(&addr);
        else
            return *reinterpret_cast<NetworkSpan<uint32_t>*>(reinterpret_cast<uint32_t*>(&addr) + 3);
    }
    const NetworkSpan<uint32_t>& v4raw() const
    {
        if constexpr (isLittleEndian)
            return *reinterpret_cast<const NetworkSpan<uint32_t>*>(&addr);
        else
            return *reinterpret_cast<const NetworkSpan<uint32_t>*>(reinterpret_cast<const uint32_t*>(&addr) + 3);
    }

    __uint128_t v6() const { return addr; }
    void setV6(__uint128_t v) { addr = v; }
    NetworkSpan<__uint128_t>& v6raw()
    {
        return *reinterpret_cast<NetworkSpan<__uint128_t>*>(&addr);
    }
    const NetworkSpan<__uint128_t>& v6raw() const
    {
        return *reinterpret_cast<const NetworkSpan<__uint128_t>*>(&addr);
    }

    bool operator==(const IPPrefix& other) const
    {
        return prefixLength == other.prefixLength && addr == other.addr;
    }

    bool operator<(const IPPrefix& other) const
    {
        if (addr != other.addr) return addr < other.addr;
        return prefixLength < other.prefixLength;
    }

    uint32_t getMask() const
    {
        if (prefixLength == 0) return 0u;
        if (prefixLength >= 32) return 0xFFFFFFFFu;
        return 0xFFFFFFFFu << (32 - prefixLength);
    }

    void addPrefixLen(uint8_t newPrefixLen)
    {
        prefixLength = newPrefixLen;
        if (isIPv4())
        {
            uint32_t v4 = v4raw();
            v4 = newPrefixLen == 0 ? 0u : v4 & (0xFFFFFFFFu << (32 - newPrefixLen));
            addr = static_cast<__uint128_t>(v4) | (__uint128_t{0xFFFF} << 32);
        }
        else
        {
            addr = newPrefixLen == 0
                ? (__uint128_t)0
                : addr & ((__uint128_t)(-1) << (128 - newPrefixLen));
        }
    }

    bool contains(const IPAddress& ip) const
    {
        if (ip.isIPv4())
        {
            if (!isIPv4()) return false;
            uint32_t mask = v4Mask(prefixLength);
            return (static_cast<uint32_t>(v4raw()) & mask) == (ip.v4() & mask);
        }
        else
        {
            if (isIPv4()) return false;
            __uint128_t mask = v6Mask(prefixLength);
            return (static_cast<__uint128_t>(v6raw()) & mask) == (ip.v6() & mask);
        }
    }

    bool contains(const IPPrefix& other) const
    {
        if (isIPv4() != other.isIPv4())
            return false;

        if (isIPv4())
        {
            uint32_t mask = v4Mask(prefixLength);
            return (static_cast<uint32_t>(other.v4raw()) & mask) == (static_cast<uint32_t>(v4raw()) & mask);
        }
        else
        {
            __uint128_t mask = v6Mask(prefixLength);
            return (static_cast<__uint128_t>(other.v6raw()) & mask) == (static_cast<__uint128_t>(v6raw()) & mask);
        }
    }
};

inline IPAddress::IPAddress(const IPPrefix& prefix)
    : raw(prefix.addr)
{}

struct alignas(4) IPv4Prefix
{
    uint32_t addr{};
    uint8_t prefixLength{};

    IPv4Prefix() = default;

    IPv4Prefix(uint32_t ip, uint8_t prefix, bool maintainAddress = false)
        : addr(ip)
    {
        if (maintainAddress)
            prefixLength = prefix;
        else
            addPrefixLen(prefix);
    }

    IPv4Prefix(const uint8_t* bytes, uint8_t prefix, bool maintainAddress = false)
    {
        if (maintainAddress)
        {
            addr = readU32(bytes);
            prefixLength = prefix;
        }
        else
        {
            addr = readBytes<uint32_t>(bytes, ((prefix + 7) / 8) * 8);
            addPrefixLen(prefix);
        }
    }

    IPv4Prefix(const IPAddress& ip, uint8_t prefix, bool maintainAddress = false)
        : addr(ip.v4())
    {
        if (maintainAddress)
            prefixLength = prefix;
        else
            addPrefixLen(prefix);
    }

    IPv4Prefix(const IPv4Address& ip, uint8_t prefix, bool maintainAddress = false)
        : addr(ip.addr)
    {
        if (maintainAddress)
            prefixLength = prefix;
        else
            addPrefixLen(prefix);
    }

    NetworkSpan<uint32_t>& raw()             { return *reinterpret_cast<NetworkSpan<uint32_t>*>(&addr); }
    const NetworkSpan<uint32_t>& raw() const { return *reinterpret_cast<const NetworkSpan<uint32_t>*>(&addr); }

    bool operator==(const IPv4Prefix& other) const
    {
        return addr == other.addr && prefixLength == other.prefixLength;
    }

    bool operator<(const IPv4Prefix& other) const
    {
        if (prefixLength != other.prefixLength) return prefixLength < other.prefixLength;
        return addr < other.addr;
    }

    void addPrefixLen(uint8_t newPrefixLen)
    {
        prefixLength = newPrefixLen;
        addr = newPrefixLen == 0 ? 0u : addr & (0xFFFFFFFFu << (32 - newPrefixLen));
    }
};

struct alignas(16) IPv6Prefix
{
    __uint128_t addr = 0;
    uint8_t prefixLength{};

    IPv6Prefix() = default;

    IPv6Prefix(__uint128_t ip, uint8_t prefix, bool maintainAddress = false)
        : addr(ip)
    {
        if (maintainAddress)
            prefixLength = prefix;
        else
            addPrefixLen(prefix);
    }

    IPv6Prefix(IPv6Address ip, uint8_t prefix, bool maintainAddress = false)
        : addr(ip.addr)
    {
        if (maintainAddress)
            prefixLength = prefix;
        else
            addPrefixLen(prefix);
    }

    IPv6Prefix(const uint8_t* bytes, uint8_t prefix, bool maintainAddress = false)
    {
        if (maintainAddress)
        {
            addr = readU128(bytes);
            prefixLength = prefix;
        }
        else
        {
            addr = readBytes<__uint128_t>(bytes, ((prefix + 7) / 8) * 8);
            addPrefixLen(prefix);
        }
    }

    NetworkSpan<__uint128_t>& raw()             { return *reinterpret_cast<NetworkSpan<__uint128_t>*>(&addr); }
    const NetworkSpan<__uint128_t>& raw() const { return *reinterpret_cast<const NetworkSpan<__uint128_t>*>(&addr); }

    bool operator==(const IPv6Prefix& other) const
    {
        return prefixLength == other.prefixLength && addr == other.addr;
    }

    bool operator<(const IPv6Prefix& other) const
    {
        if (prefixLength != other.prefixLength) return prefixLength < other.prefixLength;
        return addr < other.addr;
    }

    void addPrefixLen(uint8_t newPrefixLen)
    {
        prefixLength = newPrefixLen;
        addr = newPrefixLen == 0
            ? (__uint128_t)0
            : addr & ((__uint128_t)(-1) << (128 - newPrefixLen));
    }
};


inline IPPrefix::IPPrefix(const IPv4Prefix& prefix, bool maintainAddress)
    : addr(static_cast<__uint128_t>(prefix.addr) | (__uint128_t{0xFFFF} << 32))
{
    if (maintainAddress)
        prefixLength = prefix.prefixLength;
    else
        addPrefixLen(prefix.prefixLength);
}

inline IPPrefix::IPPrefix(const IPv6Prefix& prefix, bool maintainAddress)
    : addr(prefix.addr)
{
    if (maintainAddress)
        prefixLength = prefix.prefixLength;
    else
        addPrefixLen(prefix.prefixLength);
}

namespace std {

template <>
struct hash<IPAddress> {
    size_t operator()(const IPAddress& a) const noexcept {
        uint64_t high = static_cast<uint64_t>(a.raw >> 64);
        uint64_t low  = static_cast<uint64_t>(a.raw);
        high ^= low;
        high ^= high >> 33;
        high *= 0xff51afd7ed558ccdULL;
        high ^= high >> 33;
        high *= 0xc4ceb9fe1a85ec53ULL;
        high ^= high >> 33;
        return static_cast<size_t>(high);
    }
};

template <>
struct hash<IPv4Address> {
    size_t operator()(const IPv4Address& a) const noexcept {
        uint32_t x = a.addr;
        x ^= x >> 16;
        x *= 0x85ebca6bu;
        x ^= x >> 13;
        x *= 0xc2b2ae35u;
        x ^= x >> 16;
        return static_cast<size_t>(x);
    }
};

template <>
struct hash<IPv6Address> {
    size_t operator()(const IPv6Address& a) const noexcept {
        uint64_t high = static_cast<uint64_t>(a.addr >> 64);
        uint64_t low  = static_cast<uint64_t>(a.addr);
        high ^= low;
        high ^= high >> 33;
        high *= 0xff51afd7ed558ccdULL;
        high ^= high >> 33;
        high *= 0xc4ceb9fe1a85ec53ULL;
        high ^= high >> 33;
        return static_cast<size_t>(high);
    }
};

template <>
struct hash<IPPrefix> {
    std::size_t operator()(const IPPrefix& key) const noexcept {
        uint64_t high = static_cast<uint64_t>(key.addr >> 64);
        uint64_t low  = static_cast<uint64_t>(key.addr);
        std::size_t h = std::hash<uint8_t>{}(key.prefixLength);
        h ^= std::hash<uint64_t>{}(high) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(low)  + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

template <>
struct hash<IPv4Prefix> {
    size_t operator()(const IPv4Prefix& pfx) const noexcept {
        uint64_t h = std::hash<uint32_t>{}(pfx.addr);
        h ^= static_cast<uint64_t>(pfx.prefixLength) + 0x9e3779b7f4a7c15ull + (h << 6) + (h >> 2);
        return static_cast<size_t>(h);
    }
};

template <>
struct hash<IPv6Prefix> {
    size_t operator()(const IPv6Prefix& pfx) const noexcept {
        uint64_t high = static_cast<uint64_t>(pfx.addr >> 64);
        uint64_t low  = static_cast<uint64_t>(pfx.addr);

        size_t h1 = std::hash<uint64_t>{}(high);
        size_t h2 = std::hash<uint64_t>{}(low);
        size_t h3 = std::hash<uint8_t>{}(pfx.prefixLength);

        size_t result = h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        result ^= h3 + 0x9e3779b9 + (result << 6) + (result >> 2);
        return result;
    }
};

}

#endif
