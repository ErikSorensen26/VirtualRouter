// CliUtils.cpp

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>
#include <IPAddress.h>
#include <regex>

#include "CliUtils.h"

namespace
{
static inline bool isDec(char c)
{
    return (unsigned)(c - '0') <= 9;
}

static inline bool isHex(char c)
{
    return ((unsigned)(c - '0') <= 9) ||
           ((unsigned)(c - 'a') <= 5) ||
           ((unsigned)(c - 'A') <= 5);
}

static inline uint8_t hexVal(char c)
{
    return (c <= '9') ? static_cast<uint8_t>(c - '0') :
           (c <= 'F') ? static_cast<uint8_t>(c - 'A' + 10) :
                        static_cast<uint8_t>(c - 'a' + 10);;
}

static bool parseIPv4(const std::string& s, uint32_t& addr)
{
    const char* p = s.data();
    const char* end = p + s.size();

    uint32_t result = 0;
    uint32_t octet = 0;
    int dots = 0;
    int digits = 0;

    for (; p != end; ++p)
    {
        char c = *p;
        if (isDec(c))
        {
            octet = octet * 10 + static_cast<uint32_t>(c - '0');
            if (octet > 255) return false;
            digits++;
        }
        else if (c == '.')
        {
            if (digits == 0 || ++dots > 3) return false;
            result = (result << 8) | octet;
            octet = 0;
            digits = 0;
        }
        else return false;
    }

    if (dots != 3 || digits == 0) return false;
    result = (result << 8) | octet;
    addr = result;
    return true;
}

bool parseIPv6(const std::string& s, __uint128_t& addr)
{
    const char* p = s.data();
    const char* end = p + s.size();

    uint16_t parts[8] = {};
    int idx = 0;
    int compress = -1;

    uint16_t value = 0;
    int hexDigits = 0;

    for (; p != end; ++p)
    {
        char c = *p;

        if (isHex(c))
        {
            value = static_cast<uint16_t>(value << 4) | hexVal(c);
            if (++hexDigits > 4) return false;
        }
        else if (c == ':')
        {
            if (p + 1 < end && *(p + 1) == ':')
            {
                if (compress != -1) return false;
                compress = idx;
                ++p;
            }
            else
            {
                if (hexDigits == 0) return false;
                parts[idx++] = value;
                value = 0;
                hexDigits = 0;
            }
        }
        else
        {
            return false;
        }
    }
    
    if (hexDigits > 0)
        parts[idx++] = value;

    if (compress != -1)
    {
        int missing = 8 - idx;
        for (int i = idx - 1; i >= compress; --i)
            parts[i + missing] = parts[i];
        for (int i = 0; i < missing; ++i)
            parts[compress + i] = 0;
    }
    else if (idx != 8)
    {
        return false;
    }

    addr = 0;
    for (int i = 0; i < 8; ++i)
        addr = (addr << 16) | parts[i];

    return true;
}

bool parseIPv4Prefix(const std::string& s, uint32_t& addr, uint8_t& len)
{
    auto slash = s.find('/');
    if (slash == std::string::npos) return false;

    std::string ip = s.substr(0, slash);
    std::string plenStr = s.substr(slash + 1);

    if (parseIPv4(ip, addr)) return false;

    if (plenStr.empty()) return false;
    uint32_t plen = 0;
    for (char c : plenStr)
    {
        if (c < '0' || c > '9') return false;
        plen = plen * 10 + static_cast<uint8_t>(c - '0');
        if (plen > 32) return false;
    }
    len = static_cast<uint8_t>(plen);
    return true;
}

bool parseIPv6Prefix(const std::string& s, __uint128_t& addr, uint8_t& len)
{
    auto slash = s.find('/');
    if (slash == std::string::npos) return false;

    std::string ip = s.substr(0, slash);
    std::string plenStr = s.substr(slash + 1);

    if (!parseIPv6(ip, addr)) return false;

    if (plenStr.empty()) return false;
    uint32_t plen = 0;
    for (char c : plenStr)
    {
        if (c < '0' || c > '9') return false;
        plen = plen * 10 + static_cast<uint8_t>(c - '0');
        if (plen > 128) return false;
    }
    len = static_cast<uint8_t>(plen);
    return true;
}
}

namespace cli::utils
{
bool extractSubnetMask(uint32_t mask, uint8_t& plen)
{
    if (mask == 0)
    {
        plen = 0;
        return true;
    }

    uint8_t leadingOnes = static_cast<uint8_t>(__builtin_clz(~mask));
    uint8_t onesCount = static_cast<uint8_t>(__builtin_popcount(mask));

    if (onesCount != leadingOnes)
        return false;

    plen = onesCount;
    return true;
}

bool extractIPAddress(const std::string& str, types::IPAddress& addr)
{
    uint32_t v4 = 0;
    if (parseIPv4(str, v4))
    {
        addr.setV4(v4);
        return true;
    }
    return parseIPv6(str, addr.raw);
}

bool extractIPv4Address(const std::string& str, types::IPv4Address& addr)
{
    return parseIPv4(str, addr.addr);
}

bool extractIPv6Address(const std::string& str, types::IPv6Address& addr)
{
    return parseIPv6(str, addr.addr);
}

bool extractIPPrefix(const std::string& addr, types::IPPrefix& prefix)
{
    uint32_t v4 = 0;
    if (parseIPv4Prefix(addr, v4, prefix.prefixLength))
    {
        prefix.setV4(v4);
        return true;
    }
    return parseIPv6Prefix(addr, prefix.addr, prefix.prefixLength);
}

bool extractIPv4Prefix(const std::string& addr, types::IPv4Prefix& prefix)
{
    return parseIPv4Prefix(addr, prefix.addr, prefix.prefixLength);
}

bool extractIPv6Prefix(const std::string& addr, types::IPv6Prefix& prefix)
{
    return parseIPv6Prefix(addr, prefix.addr, prefix.prefixLength);
}

bool extractIPv4Prefix(const std::string& addr, const std::string& mask, types::IPPrefix& prefix)
{
    uint32_t maskInt = 0;
    if (!parseIPv4(mask, maskInt)) return false;
    uint32_t addrInt = 0;
    if (!parseIPv4(addr, addrInt)) return false;
    prefix.setV4(addrInt);
    if (!extractSubnetMask(maskInt, prefix.prefixLength)) return false;
    return true;
}

bool extractIPv4Prefix(const std::string& addr, const std::string& mask, types::IPv4Prefix& prefix)
{
    uint32_t maskInt = 0;
    if (!parseIPv4(mask, maskInt)) return false;
    if (!extractSubnetMask(maskInt, prefix.prefixLength)) return false;
    if (!parseIPv4(addr, prefix.addr)) return false;
    return true;
}

bool extractMacAddress(const std::string& str, uint64_t& mac)
{
    types::NetworkSpan<uint64_t> buf = *reinterpret_cast<types::NetworkSpan<uint64_t>*>(mac);
    std::string hex;

    if (str.find('.') != std::string::npos)
    {
        if (str.length() != 14 || str[4] != '.' || str[9] != '.')
            return 0;
        hex = str.substr(0, 4) + str.substr(5, 4) + str.substr(10, 4);
    }
    else
    {
        if (str.length() != 17)
            return 0;
        for (size_t i = 0; i < str.length(); i += 3)
        {
            if (i + 1 >= str.length())
                return 0;
            hex += str[i];
            hex += str[i + 1];
        }
    }

    for (size_t i = 0; i < 6; ++i)
    {
        int byte;
        std::istringstream iss(hex.substr(i * 2, 2));
        iss >> std::hex >> byte;
        if (iss.fail())
            return false;
        buf[i] = static_cast<uint8_t>(byte);
    }
    return true;
}

bool isIPv6Address(const std::string& address) 
{
    std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))");
    return std::regex_match(address, ipRegex);
}

bool isIPv6AddressWithMask(const std::string& addressWithMask) 
{
    std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))/(12[0-8]|1[01][0-9]|[1-9]?[0-9])");
    return std::regex_match(addressWithMask, ipRegex);
}

bool isMACAddress(const std::string& macAddress)
{
    std::regex macRegex(R"(^([0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}|[0-9A-Fa-f]{4}\.[0-9A-Fa-f]{4}\.[0-9A-Fa-f]{4})$)");
    return std::regex_match(macAddress, macRegex);
}

bool isNumber(const std::string& s)
{
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

[[maybe_unused]] std::optional<std::pair<std::string, std::string>> splitMiddle(const std::string& s, char delim)
{
    auto pos = s.find(delim);
    if (pos == std::string::npos) return std::nullopt;
    return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
}
}
