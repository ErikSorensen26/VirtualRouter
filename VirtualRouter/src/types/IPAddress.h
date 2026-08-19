/**
 * @file IPAddress.h
 * @brief IPv4, IPv6, and dual-stack address and prefix types used throughout the router.
 */

#ifndef IPADDRESS_H
#define IPADDRESS_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "NetworkSpan.hpp"
#include "AddressFamily.hpp"

/**
 * @defgroup TYPES Core Types
 * @brief Address, prefix, and lock-free data-structure types shared across all subsystems.
 *
 * This group contains the fundamental value types used throughout the router:
 * IP address and prefix representations (IPv4, IPv6, and dual-stack), along with
 * lock-free containers (RadixTree, LpcTrie, AtomicHashMap, AtomicStack) and
 * low-level utilities (NetworkSpan).
 *
 * None of these types own protocol state. They are plain value types or
 * purely algorithmic containers with no ties to specific subsystems.
 */
namespace types
{

#define IP_CLASS_A_PREFIX 8  ///< Default prefix length for a classful Class A network.
#define IP_CLASS_B_PREFIX 16 ///< Default prefix length for a classful Class B network.
#define IP_CLASS_C_PREFIX 24 ///< Default prefix length for a classful Class C network.

struct IPAddress;
struct IPv4Address;
struct IPv6Address;
struct IPv4Prefix;
struct IPv6Prefix;
struct IPPrefix;
struct IPv4Address;
struct IPv6Address;
struct IPv4Prefix;
struct IPv6Prefix;
struct IPPrefix;

/**
 * @brief Returns the 32-bit network mask for an IPv4 prefix length.
 *
 * @param len Prefix length in bits [0, 32].
 * @return Host-byte-order mask with the top @p len bits set.
 */
static inline uint32_t v4Mask(uint8_t len)
{
    return len == 0 ? 0u : (0xFFFFFFFFu << (32 - len));
}

/**
 * @brief Returns the 128-bit network mask for an IPv6 prefix length.
 *
 * @param len Prefix length in bits [0, 128].
 * @return Host-byte-order mask with the top @p len bits set.
 */
static inline __uint128_t v6Mask(uint8_t len)
{
    if (len == 0)
        return 0;

    return (__uint128_t(-1)) << (128 - len);
}

/**
 * @brief Dual-stack IP address that stores either an IPv4-mapped or native IPv6 address.
 * @ingroup TYPES
 *
 * IPv4 addresses are stored in IPv4-mapped form: the high 96 bits are
 * `0x0000FFFF`, and the low 32 bits hold the IPv4 address in host byte order.
 * IPv6 addresses occupy all 128 bits.
 *
 * ## Encoding
 * - IPv4: `raw = 0x0000FFFF_xxxxxxxx` where `xxxxxxxx` is the IPv4 address.
 * - IPv6: `raw` holds the full 128-bit address.
 *
 * Use @ref isIPv4() to distinguish between the two forms before calling
 * @ref v4() or @ref v6().
 */
struct IPAddress
{
    __uint128_t raw = 0; ///< Raw address bits; encoding depends on address family (see class description).

    IPAddress() = default;
    IPAddress(const IPAddress&) = default;
    IPAddress& operator=(const IPAddress&) = default;

    /**
     * @brief Constructs an IPAddress from another address, masking to the given prefix length.
     *
     * @param other Source address.
     * @param plen  Prefix length used to mask the address bits.
     */
    IPAddress(const IPAddress& other, uint8_t plen);

    /**
     * @brief Constructs an IPAddress from a prefix, masking to the given prefix length.
     *
     * @param prefix Source prefix (addr field used).
     * @param plen   Prefix length used to mask the address bits.
     */
    IPAddress(const IPPrefix& prefix, uint8_t plen);

    IPAddress(uint32_t addr);
    IPAddress(uint32_t addr, uint8_t plen);
    IPAddress(__uint128_t addr);
    IPAddress(__uint128_t addr, uint8_t plen);
    
    IPAddress(const uint8_t* bytes, AddressFamily af);
    IPAddress(const uint8_t* bytes, AddressFamily af, uint8_t plen);

    uint8_t getDefaultMask() const;

    /**
     * @brief Returns true if @p ip falls within the prefix defined by this address and @p prefixLength.
     *
     * @param ip           Host address to test.
     * @param prefixLength Number of significant prefix bits.
     */
    bool contains(IPAddress ip, uint8_t prefixLength) const;

    /**
     * @brief Masks the address in-place to @p newPrefixLen significant bits.
     *
     * @param newPrefixLen Desired prefix length [0, 128].
     */
    void addPrefixLen(uint8_t newPrefixLen);

    bool isUnspecified() const noexcept { return raw == 0; }
    bool isMulticast() const;
    bool isLocalLink() const;
    bool isGlobalUnicast() const;
    bool isLocalUnicast() const;

    bool isIPv4() const noexcept { return (raw >> 32) == 0xFFFF; } ///< True if stored as an IPv4-mapped address.
    uint32_t v4() const { return static_cast<uint32_t>(raw & 0xFFFFFFFF); } ///< Extracts the 32-bit IPv4 value.
    void setV4(uint32_t ipv4) { raw = (static_cast<__uint128_t>(ipv4)) | (__uint128_t{0xFFFF} << 32); } ///< Stores @p ipv4 in IPv4-mapped form.

    bool isIPv6() const noexcept { return !isIPv4(); } ///< True if stored as a native IPv6 address.
    __uint128_t v6() const { return raw; } ///< Returns the full 128-bit IPv6 value.
    void setV6(__uint128_t v) { raw = v; } ///< Stores a native IPv6 address.

    /**
     * @brief Returns a @ref NetworkSpan view of the IPv4 bytes in network (big-endian) order.
     *
     * @warning Only valid when @ref isIPv4() is true.
     */
    NetworkSpan<uint32_t>& v4raw();
    const NetworkSpan<uint32_t>& v4raw() const;

    /**
     * @brief Returns a @ref NetworkSpan view of the IPv6 bytes in network (big-endian) order.
     *
     * @warning Only valid when @ref isIPv6() is true.
     */
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

/**
 * @brief 32-bit IPv4 host address stored in host byte order.
 * @ingroup TYPES
 *
 * Lightweight wrapper around a `uint32_t` that provides a typed API for
 * IPv4-only code paths. Implicit conversions to/from @ref IPAddress and
 * `uint32_t` allow it to be used interchangeably with the dual-stack type.
 *
 * @see IPAddress
 * @see IPv4Prefix
 */
struct IPv4Address
{
    uint32_t addr = 0; ///< Raw 32-bit address in host byte order.

    IPv4Address() = default;
    IPv4Address(const IPv4Address&) = default;
    IPv4Address& operator=(const IPv4Address&) = default;

    /**
     * @brief Constructs from a raw 32-bit host-byte-order address.
     * @param addr Host-byte-order IPv4 address.
     */
    IPv4Address(uint32_t addr);

    /**
     * @brief Constructs from a raw address and masks to the given prefix length.
     * @param addr Host-byte-order IPv4 address.
     * @param plen Prefix length [0, 32] used to mask host bits.
     */
    IPv4Address(uint32_t addr, uint8_t plen);

    /**
     * @brief Constructs from a 4-byte big-endian buffer.
     * @param bytes Pointer to 4 bytes in network (big-endian) order.
     */
    IPv4Address(const uint8_t* bytes);

    /**
     * @brief Constructs from a 4-byte big-endian buffer, masking to @p plen bits.
     * @param bytes Pointer to 4 bytes in network (big-endian) order.
     * @param plen  Prefix length [0, 32] used to mask host bits.
     */
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

/**
 * @brief 128-bit IPv6 host address stored in host byte order.
 * @ingroup TYPES
 *
 * Lightweight wrapper around a `__uint128_t` that provides a typed API for
 * IPv6-only code paths. Implicit conversions to/from @ref IPAddress and
 * `__uint128_t` allow it to be used interchangeably with the dual-stack type.
 *
 * @see IPAddress
 * @see IPv6Prefix
 */
struct IPv6Address
{
    __uint128_t addr = 0; ///< Raw 128-bit address in host byte order.

    IPv6Address() = default;
    IPv6Address(const IPv6Address&) = default;
    IPv6Address& operator=(const IPv6Address&) = default;

    /**
     * @brief Constructs from a raw 128-bit host-byte-order address.
     * @param addr Host-byte-order IPv6 address.
     */
    IPv6Address(__uint128_t addr);

    /**
     * @brief Constructs from a raw address and masks to the given prefix length.
     * @param a    Host-byte-order IPv6 address.
     * @param plen Prefix length [0, 128] used to mask host bits.
     */
    IPv6Address(__uint128_t a, uint8_t plen);

    /**
     * @brief Constructs from a 16-byte big-endian buffer.
     * @param bytes Pointer to 16 bytes in network (big-endian) order.
     */
    IPv6Address(const uint8_t* bytes);

    /**
     * @brief Constructs from a 16-byte big-endian buffer, masking to @p plen bits.
     * @param bytes Pointer to 16 bytes in network (big-endian) order.
     * @param plen  Prefix length [0, 128] used to mask host bits.
     */
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

/**
 * @brief Dual-stack network prefix (address + prefix length).
 * @ingroup TYPES
 *
 * Stores either an IPv4 or IPv6 network prefix. IPv4 prefixes use the same
 * IPv4-mapped encoding as @ref IPAddress: bits [127:48] are zero, bits [47:32]
 * are `0xFFFF`, and bits [31:0] hold the network address.
 *
 * By default the constructor masks the host bits so that only the network
 * portion is retained. Pass `maintainAddress = true` to keep the full
 * host address alongside the prefix length.
 */
struct alignas(16) IPPrefix
{
    __uint128_t addr = 0;   ///< Network address bits; encoding matches @ref IPAddress.
    uint8_t prefixLength{}; ///< Number of significant network bits [0, 128].

    using Addr = __uint128_t; ///< Underlying address type.

    IPPrefix() = default;
    IPPrefix(const IPPrefix&) = default;
    IPPrefix(const IPv4Prefix&);
    IPPrefix(const IPv6Prefix&);
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

    /**
     * @brief Adjusts the prefix length to the classful boundary for this address.
     *
     * Applies the default classful mask (Class A = /8, B = /16, C = /24) based
     * on the high bits of the IPv4 address. Has no effect on IPv6 prefixes.
     *
     * @return True if the prefix length was modified, false if already classful.
     */
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

/**
 * @brief IPv4 network prefix: 32-bit address and prefix length.
 * @ingroup TYPES
 *
 * Stores an IPv4 network address together with its prefix length. By default
 * the constructor masks host bits so only the network portion is stored; pass
 * `maintainAddress = true` to preserve the full host address.
 *
 * @see IPPrefix
 * @see IPv4Address
 */
struct alignas(4) IPv4Prefix
{
    uint32_t addr{};        ///< Network address in host byte order; host bits are zeroed unless maintainAddress was set.
    uint8_t prefixLength{}; ///< Number of significant network bits [0, 32].

    using Addr = uint32_t; ///< Underlying address type.

    IPv4Prefix() = default;
    IPv4Prefix(const IPv4Prefix&) = default;
    IPv4Prefix& operator=(const IPv4Prefix&) = default;

    /**
     * @brief Constructs from a dual-stack @ref IPPrefix, extracting the IPv4 portion.
     * @param prefix          Source dual-stack prefix.
     * @param maintainAddress If true, host bits are preserved; otherwise the network address is masked.
     */
    explicit IPv4Prefix(const IPPrefix& prefix);

    /**
     * @brief Constructs from a dual-stack @ref IPAddress and prefix length.
     * @param ip              Host address (IPv4-mapped form expected).
     * @param prefix          Prefix length [0, 32].
     * @param maintainAddress If true, host bits are preserved.
     */
    IPv4Prefix(const IPAddress& ip, uint8_t prefix, bool maintainAddress = false);

    /**
     * @brief Constructs from a raw 32-bit address and prefix length.
     * @param ip              Host-byte-order IPv4 address.
     * @param prefix          Prefix length [0, 32].
     * @param maintainAddress If true, host bits are preserved.
     */
    IPv4Prefix(uint32_t ip, uint8_t prefix, bool maintainAddress = false);

    /**
     * @brief Constructs from a 4-byte big-endian buffer and prefix length.
     * @param bytes           Pointer to 4 bytes in network (big-endian) order.
     * @param prefix          Prefix length [0, 32].
     * @param maintainAddress If true, host bits are preserved.
     */
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

/**
 * @brief IPv6 network prefix: 128-bit address and prefix length.
 * @ingroup TYPES
 *
 * Stores an IPv6 network address together with its prefix length. By default
 * the constructor masks host bits so only the network portion is stored; pass
 * `maintainAddress = true` to preserve the full host address.
 *
 * @see IPPrefix
 * @see IPv6Address
 */
struct alignas(16) IPv6Prefix
{
    __uint128_t addr = 0;   ///< Network address in host byte order; host bits are zeroed unless maintainAddress was set.
    uint8_t prefixLength{}; ///< Number of significant network bits [0, 128].

    using Addr = __uint128_t;  ///< Underlying address type.

    IPv6Prefix() = default;
    IPv6Prefix(const IPv6Prefix&) = default;
    IPv6Prefix& operator=(const IPv6Prefix&) = default;

    /**
     * @brief Constructs from a dual-stack @ref IPPrefix, extracting the IPv6 portion.
     * @param prefix          Source dual-stack prefix.
     * @param maintainAddress If true, host bits are preserved; otherwise the network address is masked.
     */
    explicit IPv6Prefix(const IPPrefix& prefix);

    /**
     * @brief Constructs from a dual-stack @ref IPAddress and prefix length.
     * @param ip              Host-byte-order IPv6 address.
     * @param prefix          Prefix length [0, 128].
     * @param maintainAddress If true, host bits are preserved.
     */
    IPv6Prefix(const IPAddress& ip, uint8_t prefix, bool maintainAddress = false);

    /**
     * @brief Constructs from a raw 128-bit address and prefix length.
     * @param ip              Host-byte-order IPv6 address.
     * @param prefix          Prefix length [0, 128].
     * @param maintainAddress If true, host bits are preserved.
     */
    IPv6Prefix(__uint128_t ip, uint8_t prefix, bool maintainAddress = false);

    /**
     * @brief Constructs from a 16-byte big-endian buffer and prefix length.
     * @param bytes           Pointer to 16 bytes in network (big-endian) order.
     * @param prefix          Prefix length [0, 128].
     * @param maintainAddress If true, host bits are preserved.
     */
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

/**
 * @brief Satisfied by the three prefix types, ignoring cv-qualifiers and references.
 */
template <typename T>
concept IsIPPrefix =
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, IPPrefix> ||
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, IPv4Prefix> ||
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, IPv6Prefix>;

/**
 * @brief Satisfied by the three address types, ignoring cv-qualifiers and references.
 */
template <typename T>
concept IsIPAddress =
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, IPAddress> ||
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, IPv4Address> ||
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, IPv6Address>;

/**
 * @brief Maps a prefix type to its matching address type; any other type is left unchanged.
 */
template <typename T>
using PrefixToAddress =
    std::conditional_t<std::is_same_v<T, IPPrefix>, IPAddress,
        std::conditional_t<std::is_same_v<T, IPv4Prefix>, IPv4Address,
            std::conditional_t<std::is_same_v<T, IPv6Prefix>, IPv6Address, T>
        >
    >;

/**
 * @brief Maps an address type to its matching prefix type; any other type is left unchanged.
 */
template <typename T>
using AddressToPrefix =
    std::conditional_t<std::is_same_v<T, IPAddress>, IPPrefix,
        std::conditional_t<std::is_same_v<T, IPv4Address>, IPv4Prefix,
            std::conditional_t<std::is_same_v<T, IPv6Address>, IPv6Prefix, T>
        >
    >;

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

