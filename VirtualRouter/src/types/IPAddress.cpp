// IPAddress.cpp

#include <ByteUtils.hpp>
#include "IPAddress.h"

namespace types
{

IPAddress::IPAddress(const IPAddress& other, uint8_t plen)
    : IPAddress(other.raw, plen)
{}

IPAddress::IPAddress(const IPPrefix& other, uint8_t plen)
    : IPAddress(other.addr, plen)
{}

IPAddress::IPAddress(uint32_t addr)
    : raw((static_cast<__uint128_t>(addr)) | (__uint128_t{0xFFFF} << 32))
{}

IPAddress::IPAddress(uint32_t addr, uint8_t plen)
    : IPAddress(addr)
{
    addPrefixLen(plen);
}

IPAddress::IPAddress(__uint128_t addr)
    : raw(addr)
{}

IPAddress::IPAddress(__uint128_t addr, uint8_t plen)
    : raw(addr)
{
    addPrefixLen(plen);
}

IPAddress::IPAddress(const uint8_t* bytes, AddressFamily af)
{
    if (af == AddressFamily::IPv4)
    {
        uint32_t ipv4 = utils::readU32(bytes);
        raw = (static_cast<__uint128_t>(ipv4)) | (__uint128_t{0xFFFF} << 32);
    }
    else if (af == AddressFamily::IPv6)
    {
        raw = utils::readU128(bytes);
    }
    else
    {
        throw std::runtime_error("Invalid Address Family");
    }
}

IPAddress::IPAddress(const uint8_t* bytes, AddressFamily af, uint8_t plen)
    : IPAddress(bytes, af)
{
    addPrefixLen(plen);
}

bool IPAddress::contains(IPAddress ip, uint8_t prefixLength) const
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

void IPAddress::addPrefixLen(uint8_t newPrefixLen)
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

bool IPAddress::isMulticast() const
{
    if (isIPv4())
    {
        return (v4raw()[0] & 0xE0) == 0xE0;
    }
    else if (isIPv6())
    {
        return v6raw()[0] == 0xFF;
    }
    return false;
}

bool IPAddress::isLocalLink() const
{
    return isIPv6() && v6raw()[0] == 0xFE && (v6raw()[1] & 0xC0) == 0x80;
}

bool IPAddress::isGlobalUnicast() const
{
    return isIPv6() && v6raw()[0] == 0xFD;
}

bool IPAddress::isLocalUnicast() const
{
    return isIPv6() && (v6raw()[0] &0xE0) == 0x20;
}

uint8_t IPAddress::getDefaultMask() const
{
    if (!isIPv4()) return 0;
    if (v4raw()[0] <= 127) // Class A
        return IP_CLASS_A_PREFIX;
    else if (v4raw()[0] <= 191) // Class B
        return IP_CLASS_B_PREFIX;
    else if (v4raw()[0] <= 223) // Class C
        return IP_CLASS_C_PREFIX;
    else
        return 0;
}

NetworkSpan<uint32_t>& IPAddress::v4raw()
{
    if constexpr (utils::isLittleEndian)
        return *reinterpret_cast<NetworkSpan<uint32_t>*>(&raw);
    else
        return *reinterpret_cast<NetworkSpan<uint32_t>*>(reinterpret_cast<uint32_t*>(&raw) + 3);
}

const NetworkSpan<uint32_t>& IPAddress::v4raw() const
{
    if constexpr (utils::isLittleEndian)
        return *reinterpret_cast<const NetworkSpan<uint32_t>*>(&raw);
    else
        return *reinterpret_cast<const NetworkSpan<uint32_t>*>(reinterpret_cast<const uint32_t*>(&raw) + 3);
}

NetworkSpan<__uint128_t>& IPAddress::v6raw()
{
    return *reinterpret_cast<NetworkSpan<__uint128_t>*>(&raw);
}

const NetworkSpan<__uint128_t>& IPAddress::v6raw() const
{
    return *reinterpret_cast<const NetworkSpan<__uint128_t>*>(&raw);
}

bool IPAddress::operator==(const IPAddress& o) const
{
    return raw == o.raw;
}

bool IPAddress::operator!=(const IPAddress& o) const
{
    return raw != o.raw;
}

bool IPAddress::operator<(const IPAddress& o) const
{
    return raw < o.raw;
}

bool IPAddress::operator>(const IPAddress& o) const
{
    return raw > o.raw;
}

bool IPAddress::operator<=(const IPAddress& o) const
{
    return raw <= o.raw;
}

bool IPAddress::operator>=(const IPAddress& o) const
{
    return raw >= o.raw;
}

bool IPAddress::operator==(const IPv4Address& o) const
{
    return v4() == o.addr;
}

bool IPAddress::operator!=(const IPv4Address& o) const
{
    return v4() != o.addr;
}

bool IPAddress::operator<(const IPv4Address& o) const
{
    return v4() < o.addr;
}

bool IPAddress::operator>(const IPv4Address& o) const
{
    return v4() > o.addr;
}

bool IPAddress::operator<=(const IPv4Address& o) const
{
    return v4() <= o.addr;
}

bool IPAddress::operator>=(const IPv4Address& o) const
{
    return v4() >= o.addr;
}

bool IPAddress::operator==(const IPv6Address& o) const
{
    return raw == o.addr;
}

bool IPAddress::operator!=(const IPv6Address& o) const
{
    return raw != o.addr;
}

bool IPAddress::operator<(const IPv6Address& o) const
{
    return raw < o.addr;
}

bool IPAddress::operator>(const IPv6Address& o) const
{
    return raw > o.addr;
}

bool IPAddress::operator<=(const IPv6Address& o) const
{
    return raw <= o.addr;
}

bool IPAddress::operator>=(const IPv6Address& o) const
{
    return raw >= o.addr;
}

IPv4Address::IPv4Address(uint32_t a)
    : addr(a)
{}

IPv4Address::IPv4Address(uint32_t a, uint8_t plen)
    : addr(a)
{
    addPrefixLen(plen);
}

IPv4Address::IPv4Address(const uint8_t* bytes)
    : addr(utils::readU32(bytes))
{}

IPv4Address::IPv4Address(const uint8_t* bytes, uint8_t plen)
    : addr(utils::readU32(bytes))
{
    addPrefixLen(plen);
}

NetworkSpan<uint32_t>& IPv4Address::raw()
{
    return *reinterpret_cast<NetworkSpan<uint32_t>*>(&addr);
}

const NetworkSpan<uint32_t>& IPv4Address::raw() const
{
    return *reinterpret_cast<const NetworkSpan<uint32_t>*>(&addr);
}

bool IPv4Address::contains(IPv4Address ip, uint8_t prefixLen) const
{
    if (prefixLen == 0) return true;
    uint32_t mask = prefixLen == 32 ? 0xFFFFFFFFu : 0xFFFFFFFFu << (32 - prefixLen);
    return (addr & mask) == (ip.addr & mask);
}

void IPv4Address::addPrefixLen(uint8_t newPrefixLen)
{
    addr = newPrefixLen == 0 ? 0u : addr & (0xFFFFFFFFu << (32 - newPrefixLen));
}

bool IPv4Address::isMulticast() const
{
    return (raw()[0] & 0xE0) == 0xE0;
}

uint8_t IPv4Address::getDefaultMask() const
{
    if (raw()[0] <= 127) // Class A
        return IP_CLASS_A_PREFIX;
    else if (raw()[0] <= 191) // Class B
        return IP_CLASS_B_PREFIX;
    else if (raw()[0] <= 223) // Class C
        return IP_CLASS_C_PREFIX;
    else
        return 0;
}

IPv6Address::IPv6Address(__uint128_t a)
    : addr(a)
{}

IPv6Address::IPv6Address(__uint128_t a, uint8_t plen)
    : addr(a)
{
    addPrefixLen(plen);
}

IPv6Address::IPv6Address(const uint8_t* bytes)
    : addr(utils::readU128(bytes))
{}

IPv6Address::IPv6Address(const uint8_t* bytes, uint8_t plen)
    : addr(utils::readU128(bytes))
{
    addPrefixLen(plen);
}

NetworkSpan<__uint128_t>& IPv6Address::raw()
{
    return *reinterpret_cast<NetworkSpan<__uint128_t>*>(&addr);
}

const NetworkSpan<__uint128_t>& IPv6Address::raw() const
{
    return *reinterpret_cast<const NetworkSpan<__uint128_t>*>(&addr);
}

bool IPv6Address::contains(IPv6Address ip, uint8_t prefixLen)
{
    if (prefixLen == 0) return true;
    __uint128_t mask = prefixLen == 128 ? (__uint128_t)-1 : (__uint128_t(-1) << (128 - prefixLen));
    return (addr & mask) == (ip.addr & mask);
}

void IPv6Address::addPrefixLen(uint8_t newPrefixLen)
{
    addr = newPrefixLen == 0 ? (__uint128_t)0
        : addr & ((__uint128_t)(-1) << (128 - newPrefixLen));
}

bool IPv6Address::isMulticast() const
{
    return raw()[0] == 0xFF;
}

bool IPv6Address::isLocalLink() const
{
    return raw()[0] == 0xFE && (raw()[1] & 0xC0) == 0x80;
}

bool IPv6Address::isGlobalUnicast() const
{
    return raw()[0] == 0xFD;
}

bool IPv6Address::isLocalUnicast() const
{
    return (raw()[0] &0xE0) == 0x20;
}

IPPrefix::IPPrefix(AddressFamily family)
    : addr(family == AddressFamily::IPv4 ? (__uint128_t{0xFFFF} << 32) : 0)
{}

IPPrefix::IPPrefix(const IPAddress& ip, uint8_t prefix, bool maintainAddress)
    : addr(ip.raw)
{
    if (!maintainAddress)
        addPrefixLen(prefix);
    else
        prefixLength = prefix;
}

IPPrefix::IPPrefix(uint32_t ip, uint8_t prefix, bool maintainAddress)
    : addr((static_cast<__uint128_t>(ip)) | (__uint128_t{0xFFFF} << 32))
{
    if (!maintainAddress)
        addPrefixLen(prefix);
    else
        prefixLength = prefix;
}

IPPrefix::IPPrefix(__uint128_t ip, uint8_t prefix, bool maintainAddress)
    : addr(ip)
{
    if (!maintainAddress)
        addPrefixLen(prefix);
    else
        prefixLength = prefix;
}

IPPrefix::IPPrefix(const uint8_t* ip, uint8_t prefix, AddressFamily family, bool maintainAddress)
{
    if (maintainAddress)
    {
        if (family == AddressFamily::IPv4)
        {
            addr = static_cast<__uint128_t>(utils::readU32(ip)) | (__uint128_t{0xFFFF} << 32);
            prefixLength = prefix;
        }
        else
        {
            addr = utils::readU128(ip);
            prefixLength = prefix;
        }
    }
    else
    {
        if (family == AddressFamily::IPv4)
        {
            addr = utils::readBytes<uint32_t>(ip, (prefix + 7) / 8) | (__uint128_t{0xFFFF} << 32);
            addPrefixLen(prefix);
        }
        else
        {
            addr = utils::readBytes<__uint128_t>(ip, (prefix + 7) / 8);
            addPrefixLen(prefix);
        }
    }
}

NetworkSpan<uint32_t>& IPPrefix::v4raw()
{
    if constexpr (utils::isLittleEndian)
        return *reinterpret_cast<NetworkSpan<uint32_t>*>(&addr);
    else
        return *reinterpret_cast<NetworkSpan<uint32_t>*>(reinterpret_cast<uint32_t*>(&addr) + 3);
}

const NetworkSpan<uint32_t>& IPPrefix::v4raw() const
{
    if constexpr (utils::isLittleEndian)
        return *reinterpret_cast<const NetworkSpan<uint32_t>*>(&addr);
    else
        return *reinterpret_cast<const NetworkSpan<uint32_t>*>(reinterpret_cast<const uint32_t*>(&addr) + 3);
}

NetworkSpan<__uint128_t>& IPPrefix::v6raw()
{
    return *reinterpret_cast<NetworkSpan<__uint128_t>*>(&addr);
}

const NetworkSpan<__uint128_t>& IPPrefix::v6raw() const
{
    return *reinterpret_cast<const NetworkSpan<__uint128_t>*>(&addr);
}

uint32_t IPPrefix::getMask() const
{
    if (prefixLength == 0) return 0u;
    if (prefixLength >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32 - prefixLength);
}

uint8_t IPPrefix::getDefaultMask() const
{
    if (!isIPv4()) return 0;
    if (v4raw()[0] <= 127) // Class A
        return IP_CLASS_A_PREFIX;
    else if (v4raw()[0] <= 191) // Class B
        return IP_CLASS_B_PREFIX;
    else if (v4raw()[0] <= 223) // Class C
        return IP_CLASS_C_PREFIX;
    else
        return 0;
}

bool IPPrefix::applyClassful()
{
    if (!isIPv4()) return false;
    if (v4raw()[0] <= 127) // Class A
        addPrefixLen(IP_CLASS_A_PREFIX);
    else if (v4raw()[0] <= 191) // Class B
        addPrefixLen(IP_CLASS_B_PREFIX);
    else if (v4raw()[0] <= 223) // Class C
        addPrefixLen(IP_CLASS_C_PREFIX) ;
    else
        return false;
    return true;
}

bool IPPrefix::contains(const IPPrefix& other) const
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

bool IPPrefix::contains(const IPAddress& ip) const
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

void IPPrefix::addPrefixLen(uint8_t newPrefixLen)
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

bool IPPrefix::isMulticast() const
{
    if (isIPv4())
    {
        return (v4raw()[0] & 0xE0) == 0xE0;
    }
    else if (isIPv6())
    {
        return v6raw()[0] == 0xFF;
    }
    return false;
}

bool IPPrefix::isLocalLink() const
{
    return isIPv6() && v6raw()[0] == 0xFE && (v6raw()[1] & 0xC0) == 0x80;
}

bool IPPrefix::isGlobalUnicast() const
{
    return isIPv6() && v6raw()[0] == 0xFD;
}

bool IPPrefix::isLocalUnicast() const
{
    return isIPv6() && (v6raw()[0] &0xE0) == 0x20;
}

bool IPPrefix::operator==(const IPPrefix& o) const
{
    return addr == o.addr && prefixLength == o.prefixLength;
}

bool IPPrefix::operator!=(const IPPrefix& o) const
{
    return addr != o.addr || prefixLength != o.prefixLength;
}

bool IPPrefix::operator<(const IPPrefix& o) const
{
    if (addr != o.addr) return addr < o.addr;
    return prefixLength < o.prefixLength;
}

bool IPPrefix::operator>(const IPPrefix& o) const
{
    if (addr != o.addr) return addr > o.addr;
    return prefixLength > o.prefixLength;
}

bool IPPrefix::operator<=(const IPPrefix& o) const
{
    if (*this == o) return true;
    return *this < o;
}

bool IPPrefix::operator>=(const IPPrefix& o) const
{
    if (*this == o) return true;
    return *this > o;
}

bool IPPrefix::operator==(const IPv4Prefix& o) const
{
    return v4() == o.addr && prefixLength == o.prefixLength;
}

bool IPPrefix::operator!=(const IPv4Prefix& o) const
{
    return v4() != o.addr || prefixLength != o.prefixLength;
}

bool IPPrefix::operator< (const IPv4Prefix& o) const
{
    if (v4() != o.addr) return v4() < o.addr;
    return prefixLength < o.prefixLength;
}

bool IPPrefix::operator> (const IPv4Prefix& o) const
{
    if (v4() != o.addr) return v4() > o.addr;
    return prefixLength > o.prefixLength;
}

bool IPPrefix::operator<=(const IPv4Prefix& o) const
{
    if (*this == o) return true;
    return *this < o;
}

bool IPPrefix::operator>=(const IPv4Prefix& o) const
{
    if (*this == o) return true;
    return *this > o;
}

bool IPPrefix::operator==(const IPv6Prefix& o) const
{
    return addr == o.addr && prefixLength == o.prefixLength;
}

bool IPPrefix::operator!=(const IPv6Prefix& o) const
{
    return addr != o.addr || prefixLength != o.prefixLength;
}

bool IPPrefix::operator< (const IPv6Prefix& o) const
{
    if (addr == o.addr) return addr < o.addr;
    return prefixLength < o.addr;
}

bool IPPrefix::operator> (const IPv6Prefix& o) const
{
    if (addr == o.addr) return addr > o.addr;
    return prefixLength > o.prefixLength;
}

bool IPPrefix::operator<=(const IPv6Prefix& o) const
{
    if (*this == o) return true;
    return *this < o;
}

bool IPPrefix::operator>=(const IPv6Prefix& o) const
{
    if (*this == o) return true;
    return *this > o;
}

IPv4Prefix::IPv4Prefix(const IPAddress& ip, uint8_t prefix, bool maintainAddress)
    : addr(ip.v4())
{
    if (!maintainAddress)
        addPrefixLen(prefix);
    else
        prefixLength = prefix;
}

IPv4Prefix::IPv4Prefix(const IPPrefix& prefix, bool maintainAddress)
    : addr(prefix.v4())
{
    if (!maintainAddress) 
        addPrefixLen(prefix.prefixLength);
    else
        prefixLength = prefix.prefixLength;
}

IPv4Prefix::IPv4Prefix(uint32_t ip, uint8_t prefix, bool maintainAddress)
    : addr(ip)
{
    if (!maintainAddress)
        addPrefixLen(prefix);
    else
        prefixLength = prefix;
}

IPv4Prefix::IPv4Prefix(const uint8_t* bytes, uint8_t prefix, bool maintainAddress)
{
    if (maintainAddress)
    {
        addr = utils::readU32(bytes);
        prefixLength = prefix;
    }
    else
    {
        addr = utils::readBytes<uint32_t>(bytes, (prefix + 7) / 8);
        addPrefixLen(prefix);
    }
}

NetworkSpan<uint32_t>& IPv4Prefix::raw()
{
    return *reinterpret_cast<NetworkSpan<uint32_t>*>(&addr);
}

const NetworkSpan<uint32_t>& IPv4Prefix::raw() const
{
    return *reinterpret_cast<const NetworkSpan<uint32_t>*>(&addr);
}

uint32_t IPv4Prefix::getMask() const
{
    if (prefixLength == 0) return 0u;
    if (prefixLength >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32 - prefixLength);
}

uint8_t IPv4Prefix::getDefaultMask() const
{
    if (raw()[0] <= 127) // Class A
        return IP_CLASS_A_PREFIX;
    else if (raw()[0] <= 191) // Class B
        return IP_CLASS_B_PREFIX;
    else if (raw()[0] <= 223) // Class C
        return IP_CLASS_C_PREFIX;
    else
        return 0;
}

bool IPv4Prefix::applyClassful()
{
    if (raw()[0] <= 127) // Class A
        addPrefixLen(IP_CLASS_A_PREFIX);
    else if (raw()[0] <= 191) // Class B
        addPrefixLen(IP_CLASS_B_PREFIX);
    else if (raw()[0] <= 223) // Class C
        addPrefixLen(IP_CLASS_C_PREFIX) ;
    else
        return false;
    return true;
}

bool IPv4Prefix::contains(IPv4Prefix other) const
{
    uint32_t mask = v4Mask(prefixLength);
    return (other.addr & mask) == (addr & mask);
}

bool IPv4Prefix::contains(IPv4Address ip) const
{
    uint32_t mask = v4Mask(prefixLength);
    return (addr & mask) == (ip.addr & mask);
}

void IPv4Prefix::addPrefixLen(uint8_t newPrefixLen)
{
    prefixLength = newPrefixLen;
    addr = newPrefixLen == 0 ? 0u : addr & (0xFFFFFFFFu << (32 - newPrefixLen));
}

bool IPv4Prefix::isMulticast() const
{
    return (raw()[0] & 0xE0) == 0xE0;
}

bool IPv4Prefix::operator==(const IPv4Prefix& o) const
{
    return addr == o.addr && prefixLength == o.prefixLength;
}

bool IPv4Prefix::operator!=(const IPv4Prefix& o) const
{
    return addr != o.addr || prefixLength != o.prefixLength;
}

bool IPv4Prefix::operator<(const IPv4Prefix& o) const
{
    if (addr != o.addr) return addr < o.addr;
    return prefixLength < o.prefixLength;
}

bool IPv4Prefix::operator>(const IPv4Prefix& o) const
{
    if (addr != o.addr) return addr > o.addr;
    return prefixLength > o.prefixLength;
}

bool IPv4Prefix::operator<=(const IPv4Prefix& o) const
{
    if (*this == o) return true;
    return *this < o;
}

bool IPv4Prefix::operator>=(const IPv4Prefix& o) const
{
    if (*this == o) return true;
    return *this > o;
}

bool IPv4Prefix::operator==(const IPPrefix& o) const
{
    return addr == o.v4() && prefixLength == o.prefixLength;
}

bool IPv4Prefix::operator!=(const IPPrefix& o) const
{
    return addr != o.v4() || prefixLength != o.prefixLength;
}

bool IPv4Prefix::operator<(const IPPrefix& o) const
{
    if (addr != o.v4()) return addr < o.v4();
    return prefixLength < o.prefixLength;
}

bool IPv4Prefix::operator>(const IPPrefix& o) const
{
    if (addr != o.v4()) return addr > o.v4();
    return prefixLength > o.prefixLength;
}

bool IPv4Prefix::operator<=(const IPPrefix& o) const
{
    if (*this == o) return true;
    return *this < o;
}

bool IPv4Prefix::operator>=(const IPPrefix& o) const
{
    if (*this == o) return true;
    return *this > o;
}

IPv6Prefix::IPv6Prefix(const IPPrefix& prefix, bool maintainAddress)
    : addr(prefix.v6())
{
    if (!maintainAddress)
        addPrefixLen(prefix.prefixLength);
    else
        prefixLength = prefix.prefixLength;
}

IPv6Prefix::IPv6Prefix(const IPAddress& ip, uint8_t prefix, bool maintainAddress)
    : addr(ip.raw)
{
    if (!maintainAddress)
        addPrefixLen(prefix);
    else
        prefixLength = prefix;
}

IPv6Prefix::IPv6Prefix(__uint128_t ip, uint8_t prefix, bool maintainAddress)
    : addr(ip)
{
    if (maintainAddress)
        prefixLength = prefix;
    else
        addPrefixLen(prefix);
}

IPv6Prefix::IPv6Prefix(const uint8_t* bytes, uint8_t prefix, bool maintainAddress)
{
    if (maintainAddress)
    {
        addr = utils::readU128(bytes);
        prefixLength = prefix;
    }
    else
    {
        addr = utils::readBytes<__uint128_t>(bytes, (prefix + 7) / 8);
        addPrefixLen(prefix);
    }
}

NetworkSpan<__uint128_t>& IPv6Prefix::raw()
{
    return *reinterpret_cast<NetworkSpan<__uint128_t>*>(&addr);
}

const NetworkSpan<__uint128_t>& IPv6Prefix::raw() const
{
    return *reinterpret_cast<const NetworkSpan<__uint128_t>*>(&addr);
}

bool IPv6Prefix::contains(const IPv6Prefix& other) const
{
    if (prefixLength == 0) return true;
    __uint128_t mask = prefixLength == 128 ? (__uint128_t)-1 : (__uint128_t(-1) << (128 - prefixLength));
    return (addr & mask) == (other.addr & mask);
}

bool IPv6Prefix::contains(const IPv6Address& ip) const
{
    if (prefixLength == 0) return true;
    __uint128_t mask = prefixLength == 128 ? (__uint128_t)-1 : (__uint128_t(-1) << (128 - prefixLength));
    return (addr & mask) == (ip.addr & mask);
}

void IPv6Prefix::addPrefixLen(uint8_t newPrefixLen)
{
    addr = newPrefixLen == 0
        ? (__uint128_t)0 : addr & ((__uint128_t)(-1) << (128 - newPrefixLen));
}

bool IPv6Prefix::isMulticast() const
{
    return raw()[0] == 0xFF;
}

bool IPv6Prefix::isLocalLink() const
{
    return raw()[0] == 0xFE && (raw()[1] & 0xC0) == 0x80;
}

bool IPv6Prefix::isGlobalUnicast() const
{
    return raw()[0] == 0xFD;
}

bool IPv6Prefix::isLocalUnicast() const
{
    return (raw()[0] &0xE0) == 0x20;
}

bool IPv6Prefix::operator==(const IPv6Prefix& o) const
{
    return addr == o.addr && prefixLength == o.prefixLength;
}

bool IPv6Prefix::operator!=(const IPv6Prefix& o) const
{
    return addr != o.addr || prefixLength != o.prefixLength;
}

bool IPv6Prefix::operator<(const IPv6Prefix& o) const
{
    if (addr != o.addr) return addr < o.addr;
    return prefixLength < o.prefixLength;
}

bool IPv6Prefix::operator>(const IPv6Prefix& o) const
{
    if (addr != o.addr) return addr > o.addr;
    return prefixLength > o.prefixLength;
}

bool IPv6Prefix::operator<=(const IPv6Prefix& o) const
{
    if (*this == o) return true;
    return *this < o;
}

bool IPv6Prefix::operator>=(const IPv6Prefix& o) const
{
    if (*this == o) return true;
    return *this > o;
}

bool IPv6Prefix::operator==(const IPPrefix& o) const
{
    return addr == o.addr && prefixLength == o.prefixLength;
}

bool IPv6Prefix::operator!=(const IPPrefix& o) const
{
    return addr != o.addr || prefixLength != o.prefixLength;
}

bool IPv6Prefix::operator<(const IPPrefix& o) const
{
    if (addr != o.addr) return addr < o.addr;
    return prefixLength < o.prefixLength;
}

bool IPv6Prefix::operator>(const IPPrefix& o) const
{
    if (addr != o.addr) return addr > o.addr;
    return prefixLength > o.prefixLength;
}

bool IPv6Prefix::operator<=(const IPPrefix& o) const
{
    if (*this == o) return true;
    return *this < o;
}

bool IPv6Prefix::operator>=(const IPPrefix& o) const
{
    if (*this == o) return true;
    return *this > o;
}
} // namespace types
