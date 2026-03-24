// IPAddress.h

#ifndef IPADDRESS_H
#define IPADDRESS_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "NetworkSpan.hpp"
#include "AddressFamily.hpp"

namespace types
{

#define IP_CLASS_A_PREFIX 8
#define IP_CLASS_B_PREFIX 16
#define IP_CLASS_C_PREFIX 24

struct IPAddress;
struct IPv4Address;
struct IPv6Address;
struct IPv4Prefix;
struct IPv6Prefix;
struct IPPrefix;

static inline uint32_t v4Mask(uint8_t len)
{
    return len == 0 ? 0u : (0xFFFFFFFFu << (32 - len));
}

static inline __uint128_t v6Mask(uint8_t len)
{
    if (len == 0)
        return 0;

    return (__uint128_t(-1)) << (128 - len);
}

struct IPAddress
{
    __uint128_t raw = 0;

    IPAddress() = default;
    IPAddress(const IPAddress&) = default;
    IPAddress& operator=(const IPAddress&) = default;

    IPAddress(const IPAddress& other, uint8_t plen);
    IPAddress(const IPPrefix& prefix, uint8_t plen);

    IPAddress(uint32_t addr);
    IPAddress(uint32_t addr, uint8_t plen);
    IPAddress(__uint128_t addr);
    IPAddress(__uint128_t addr, uint8_t plen);
    
    IPAddress(const uint8_t* bytes, AddressFamily af);
    IPAddress(const uint8_t* bytes, AddressFamily af, uint8_t plen);

    uint8_t getDefaultMask() const;
    bool contains(IPAddress ip, uint8_t prefixLength) const;
    void addPrefixLen(uint8_t newPrefixLen);
    bool isUnspecified() const noexcept { return raw == 0; }
    bool isMulticast() const;
    bool isLocalLink() const;
    bool isGlobalUnicast() const;
    bool isLocalUnicast() const;

    bool isIPv4() const noexcept { return (raw >> 32) == 0xFFFF; }
    uint32_t v4() const { return static_cast<uint32_t>(raw & 0xFFFFFFFF); }
    void setV4(uint32_t ipv4) { raw = (static_cast<__uint128_t>(ipv4)) | (__uint128_t{0xFFFF} << 32); }

    bool isIPv6() const noexcept { return !isIPv4(); }
    __uint128_t v6() const { return raw; }
    void setV6(__uint128_t v) { raw = v; }

    NetworkSpan<uint32_t>& v4raw();
    const NetworkSpan<uint32_t>& v4raw() const;

    NetworkSpan<__uint128_t>& v6raw();
    const NetworkSpan<__uint128_t>& v6raw() const;

    operator uint32_t() const { return v4(); }
    operator __uint128_t() const { return v6(); }

    bool operator==(const IPAddress& o) const;
    bool operator!=(const IPAddress& o) const;
    bool operator<(const IPAddress& o) const;
    bool operator>(const IPAddress& o) const;
    bool operator<=(const IPAddress& o) const;
    bool operator>=(const IPAddress& o) const;

    bool operator==(const IPv4Address& o) const;
    bool operator!=(const IPv4Address& o) const;
    bool operator<(const IPv4Address& o) const;
    bool operator>(const IPv4Address& o) const;
    bool operator<=(const IPv4Address& o) const;
    bool operator>=(const IPv4Address& o) const;

    bool operator==(const IPv6Address& o) const;
    bool operator!=(const IPv6Address& o) const;
    bool operator<(const IPv6Address& o) const;
    bool operator>(const IPv6Address& o) const;
    bool operator<=(const IPv6Address& o) const;
    bool operator>=(const IPv6Address& o) const;
};

struct IPv4Address
{
    uint32_t addr = 0;

    IPv4Address() = default;
    IPv4Address(const IPv4Address&) = default;
    IPv4Address& operator=(const IPv4Address&) = default;

    IPv4Address(uint32_t addr);
    IPv4Address(uint32_t addr, uint8_t plen);

    IPv4Address(const uint8_t* bytes);
    IPv4Address(const uint8_t* bytes, uint8_t plen);

    NetworkSpan<uint32_t>& raw();
    const NetworkSpan<uint32_t>& raw() const;

    uint8_t getDefaultMask() const;
    bool contains(IPv4Address ip, uint8_t prefixLen) const;
    void addPrefixLen(uint8_t newPrefixLen);
    bool isUnspecified() const noexcept { return addr == 0; }
    bool isMulticast() const;

    operator IPAddress() const { return IPAddress(addr); }
    operator uint32_t() const { return addr; }

    bool operator==(const IPv4Address& o) const { return addr == o.addr; }
    bool operator!=(const IPv4Address& o) const { return addr != o.addr; }
    bool operator< (const IPv4Address& o) const { return addr <  o.addr; }
    bool operator> (const IPv4Address& o) const { return addr >  o.addr; }
    bool operator<=(const IPv4Address& o) const { return addr <= o.addr; }
    bool operator>=(const IPv4Address& o) const { return addr >= o.addr; }

    bool operator==(const IPAddress& o) const { return addr == o.v4(); }
    bool operator!=(const IPAddress& o) const { return addr != o.v4(); }
    bool operator< (const IPAddress& o) const { return addr <  o.v4(); }
    bool operator> (const IPAddress& o) const { return addr >  o.v4(); }
    bool operator<=(const IPAddress& o) const { return addr <= o.v4(); }
    bool operator>=(const IPAddress& o) const { return addr >= o.v4(); }

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
    IPv6Address(const IPv6Address&) = default;
    IPv6Address& operator=(const IPv6Address&) = default;

    IPv6Address(__uint128_t addr);
    IPv6Address(__uint128_t a, uint8_t plen);

    IPv6Address(const uint8_t* bytes);
    IPv6Address(const uint8_t* bytes, uint8_t plen);

    NetworkSpan<__uint128_t>& raw();
    const NetworkSpan<__uint128_t>& raw() const;

    bool contains(IPv6Address ip, uint8_t prefixLen);
    void addPrefixLen(uint8_t newPrefixLen);
    bool isUnspecified() const noexcept { return addr == 0; }
    bool isMulticast() const;
    bool isLocalLink() const;
    bool isGlobalUnicast() const;
    bool isLocalUnicast() const;

    operator IPAddress() const { return IPAddress(addr); }
    operator __uint128_t() const { return addr; }

    bool operator==(const IPv6Address& o) const { return addr == o.addr; }
    bool operator!=(const IPv6Address& o) const { return addr != o.addr; }
    bool operator< (const IPv6Address& o) const { return addr <  o.addr; }
    bool operator> (const IPv6Address& o) const { return addr >  o.addr; }
    bool operator<=(const IPv6Address& o) const { return addr <= o.addr; }
    bool operator>=(const IPv6Address& o) const { return addr >= o.addr; }

    bool operator==(const IPAddress& o) const { return addr == o.raw; }
    bool operator!=(const IPAddress& o) const { return addr != o.raw; }
    bool operator< (const IPAddress& o) const { return addr <  o.raw; }
    bool operator> (const IPAddress& o) const { return addr >  o.raw; }
    bool operator<=(const IPAddress& o) const { return addr <= o.raw; }
    bool operator>=(const IPAddress& o) const { return addr >= o.raw; }

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
    IPPrefix(const IPPrefix&) = default;
    IPPrefix& operator=(const IPPrefix&) = default;
    explicit IPPrefix(AddressFamily family);

    IPPrefix(const IPAddress& ip, uint8_t prefix, bool maintainAddress = false);
    IPPrefix(uint32_t ip, uint8_t prefix, bool maintainAddress = false);
    IPPrefix(__uint128_t ip, uint8_t prefix, bool maintainAddress = false);

    IPPrefix(const uint8_t* ip, uint8_t prefix, AddressFamily family, bool maintainAddress = false);

    bool isIPv4() const noexcept { return (addr >> 32) == 0xFFFF; }
    uint32_t v4() const { return static_cast<uint32_t>(addr & 0xFFFFFFFF); }
    void setV4(uint32_t ipv4) { addr = (static_cast<__uint128_t>(ipv4)) | (__uint128_t{0xFFFF} << 32); }

    bool isIPv6() const noexcept { return !isIPv4(); }
    __uint128_t v6() const { return addr; }
    void setV6(__uint128_t v) { addr = v; }

    NetworkSpan<uint32_t>& v4raw();
    const NetworkSpan<uint32_t>& v4raw() const;

    NetworkSpan<__uint128_t>& v6raw();
    const NetworkSpan<__uint128_t>& v6raw() const;

    uint32_t getMask() const;
    uint8_t getDefaultMask() const;
    bool applyClassful();
    bool contains(const IPPrefix& ip) const;
    bool contains(const IPAddress& ip) const;
    void addPrefixLen(uint8_t newPrefixLen);
    bool isUnspecified() const noexcept { return addr == 0; }
    bool isMulticast() const;
    bool isLocalLink() const;
    bool isGlobalUnicast() const;
    bool isLocalUnicast() const;

    operator IPAddress() const { return IPAddress(addr); };
    operator IPv4Address() const { return IPv4Address(v4()); }
    operator IPv6Address() const { return IPv6Address(v6()); }
    operator uint32_t() const { return v4(); }
    operator __uint128_t() const { return v6(); }

    bool operator==(const IPPrefix& o) const;
    bool operator!=(const IPPrefix& o) const; 
    bool operator< (const IPPrefix& o) const; 
    bool operator> (const IPPrefix& o) const; 
    bool operator<=(const IPPrefix& o) const; 
    bool operator>=(const IPPrefix& o) const; 

    bool operator==(const IPv4Prefix& o) const;
    bool operator!=(const IPv4Prefix& o) const; 
    bool operator< (const IPv4Prefix& o) const; 
    bool operator> (const IPv4Prefix& o) const; 
    bool operator<=(const IPv4Prefix& o) const; 
    bool operator>=(const IPv4Prefix& o) const; 

    bool operator==(const IPv6Prefix& o) const;
    bool operator!=(const IPv6Prefix& o) const; 
    bool operator< (const IPv6Prefix& o) const; 
    bool operator> (const IPv6Prefix& o) const; 
    bool operator<=(const IPv6Prefix& o) const; 
    bool operator>=(const IPv6Prefix& o) const; 
};

struct alignas(4) IPv4Prefix
{
    uint32_t addr{};
    uint8_t prefixLength{};

    IPv4Prefix() = default;
    IPv4Prefix(const IPv4Prefix&) = default;
    IPv4Prefix& operator=(const IPv4Prefix&) = default;

    IPv4Prefix(const IPPrefix& prefix, bool maintainAddress = false);
    IPv4Prefix(const IPAddress& ip, uint8_t prefix, bool maintainAddress = false);
    IPv4Prefix(uint32_t ip, uint8_t prefix, bool maintainAddress = false);

    IPv4Prefix(const uint8_t* bytes, uint8_t prefix, bool maintainAddress = false);

    NetworkSpan<uint32_t>& raw();
    const NetworkSpan<uint32_t>& raw() const;

    uint32_t getMask() const;
    uint8_t getDefaultMask() const;
    bool applyClassful();
    bool contains(IPv4Prefix other) const;
    bool contains(IPv4Address ip) const;
    void addPrefixLen(uint8_t prefixLen);
    bool isUnspecified() const noexcept { return addr == 0; }
    bool isMulticast() const;

    operator IPAddress() const { return IPAddress(addr); }
    operator IPv4Address() const { return IPv4Address(addr); }
    operator uint32_t() const { return addr; }

    bool operator==(const IPv4Prefix& o) const;
    bool operator!=(const IPv4Prefix& o) const;
    bool operator<(const IPv4Prefix& o) const;
    bool operator>(const IPv4Prefix& o) const;
    bool operator<=(const IPv4Prefix& o) const;
    bool operator>=(const IPv4Prefix& o) const;

    bool operator==(const IPPrefix& o) const;
    bool operator!=(const IPPrefix& o) const;
    bool operator<(const IPPrefix& o) const;
    bool operator>(const IPPrefix& o) const;
    bool operator<=(const IPPrefix& o) const;
    bool operator>=(const IPPrefix& o) const;
};

struct alignas(16) IPv6Prefix
{
    __uint128_t addr = 0;
    uint8_t prefixLength{};

    IPv6Prefix() = default;
    IPv6Prefix(const IPv6Prefix&) = default;
    IPv6Prefix& operator=(const IPv6Prefix&) = default;

    IPv6Prefix(const IPPrefix& prefix, bool maintainAddress = false);
    IPv6Prefix(const IPAddress& ip, uint8_t prefix, bool maintainAddress = false);
    IPv6Prefix(__uint128_t ip, uint8_t prefix, bool maintainAddress = false);

    IPv6Prefix(const uint8_t* bytes, uint8_t prefix, bool maintainAddress = false);

    NetworkSpan<__uint128_t>& raw();
    const NetworkSpan<__uint128_t>& raw() const;

    bool contains(const IPv6Prefix& ip) const;
    bool contains(const IPv6Address& ip) const;
    void addPrefixLen(uint8_t prefixLen);
    bool isUnspecified() const noexcept { return addr == 0; }
    bool isMulticast() const;
    bool isLocalLink() const;
    bool isGlobalUnicast() const;
    bool isLocalUnicast() const;

    operator IPAddress() const { return IPAddress(addr); }
    operator IPv6Address() const { return IPv6Address(addr); }
    operator __uint128_t() const { return addr; }

    bool operator==(const IPv6Prefix& o) const;
    bool operator!=(const IPv6Prefix& o) const;
    bool operator<(const IPv6Prefix& o) const;
    bool operator>(const IPv6Prefix& o) const;
    bool operator<=(const IPv6Prefix& o) const;
    bool operator>=(const IPv6Prefix& o) const;

    bool operator==(const IPPrefix& o) const;
    bool operator!=(const IPPrefix& o) const;
    bool operator<(const IPPrefix& o) const;
    bool operator>(const IPPrefix& o) const;
    bool operator<=(const IPPrefix& o) const;
    bool operator>=(const IPPrefix& o) const;
};

template <typename T>
constexpr bool isIpPrefix()
{
    using U = std::remove_cv_t<std::remove_reference_t<T>> ;
    return std::is_same_v<U, IPPrefix> ||
           std::is_same_v<U, IPv4Prefix> ||
           std::is_same_v<U, IPv6Prefix>;
}

} // namespace types

namespace std {

template <>
struct hash<types::IPAddress> {
    size_t operator()(const types::IPAddress& a) const noexcept {
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
struct hash<types::IPv4Address> {
    size_t operator()(const types::IPv4Address& a) const noexcept {
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
struct hash<types::IPv6Address> {
    size_t operator()(const types::IPv6Address& a) const noexcept {
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
struct hash<types::IPPrefix> {
    std::size_t operator()(const types::IPPrefix& key) const noexcept {
        uint64_t high = static_cast<uint64_t>(key.addr >> 64);
        uint64_t low  = static_cast<uint64_t>(key.addr);
        std::size_t h = std::hash<uint8_t>{}(key.prefixLength);
        h ^= std::hash<uint64_t>{}(high) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(low)  + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

template <>
struct hash<types::IPv4Prefix> {
    size_t operator()(const types::IPv4Prefix& pfx) const noexcept {
        uint64_t h = std::hash<uint32_t>{}(pfx.addr);
        h ^= static_cast<uint64_t>(pfx.prefixLength) + 0x9e3779b7f4a7c15ull + (h << 6) + (h >> 2);
        return static_cast<size_t>(h);
    }
};

template <>
struct hash<types::IPv6Prefix> {
    size_t operator()(const types::IPv6Prefix& pfx) const noexcept {
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

#endif // IPADDRESS_H

