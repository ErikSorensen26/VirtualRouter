// CliUtils.cpp

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>
#include <IPAddress.h>
#include <Mac.hpp>
#include <regex>
#include <Global.h>

#include "cli/runtime/CliSession.h"
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

static bool parseIPv4(std::string_view s, uint32_t& addr)
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

bool parseIPv6(std::string_view s, __uint128_t& addr)
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

bool parseIPv4Prefix(std::string_view s, uint32_t& addr, uint8_t& len)
{
    auto slash = s.find('/');
    if (slash == std::string::npos) return false;

    std::string_view ip = s.substr(0, slash);
    std::string_view plenStr = s.substr(slash + 1);

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

bool parseIPv6Prefix(std::string_view s, __uint128_t& addr, uint8_t& len)
{
    auto slash = s.find('/');
    if (slash == std::string::npos) return false;

    std::string_view ip = s.substr(0, slash);
    std::string_view plenStr = s.substr(slash + 1);

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
bool extractInterfaceId(std::string_view typeStr, std::string_view idStr, interface::InterfaceKey& key)
{
    interface::InterfaceType type = interface::getInterfaceType(typeStr);
    if (type == interface::InterfaceType::UNDEFINED) return false;
    float id;
    if (!utils::stofloat(id, idStr))
        return false;
    key = {type, id};
    return true;
}

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

bool extractIPAddress(std::string_view str, types::IPAddress& addr)
{
    uint32_t v4 = 0;
    if (parseIPv4(str, v4))
    {
        addr.setV4(v4);
        return true;
    }
    return parseIPv6(str, addr.raw);
}

bool extractIPv4Address(std::string_view str, types::IPv4Address& addr)
{
    return parseIPv4(str, addr.addr);
}

bool extractIPv6Address(std::string_view str, types::IPv6Address& addr)
{
    return parseIPv6(str, addr.addr);
}

bool extractIPPrefix(std::string_view addr, types::IPPrefix& prefix)
{
    uint32_t v4 = 0;
    if (parseIPv4Prefix(addr, v4, prefix.prefixLength))
    {
        prefix.setV4(v4);
        return true;
    }
    return parseIPv6Prefix(addr, prefix.addr, prefix.prefixLength);
}

bool extractIPv4Prefix(std::string_view addr, types::IPv4Prefix& prefix)
{
    return parseIPv4Prefix(addr, prefix.addr, prefix.prefixLength);
}

bool extractIPv6Prefix(std::string_view addr, types::IPv6Prefix& prefix)
{
    return parseIPv6Prefix(addr, prefix.addr, prefix.prefixLength);
}

bool extractIPv4Prefix(std::string_view addr, std::string_view mask, types::IPPrefix& prefix)
{
    uint32_t maskInt = 0;
    if (!parseIPv4(mask, maskInt)) return false;
    uint32_t addrInt = 0;
    if (!parseIPv4(addr, addrInt)) return false;
    prefix.setV4(addrInt);
    if (!extractSubnetMask(maskInt, prefix.prefixLength)) return false;
    return true;
}

bool extractIPv4Prefix(std::string_view addr, std::string_view mask, types::IPv4Prefix& prefix)
{
    uint32_t maskInt = 0;
    if (!parseIPv4(mask, maskInt)) return false;
    if (!extractSubnetMask(maskInt, prefix.prefixLength)) return false;
    if (!parseIPv4(addr, prefix.addr)) return false;
    return true;
}

bool extractMacAddress(std::string_view str, types::Mac mac)
{
    types::NetworkSpan<uint64_t> buf = *reinterpret_cast<types::NetworkSpan<uint64_t>*>(mac.mac);
    std::string hex;

    if (str.find('.') != std::string::npos)
    {
        if (str.length() != 14 || str[4] != '.' || str[9] != '.')
            return 0;
        hex = std::string(str.substr(0, 4)) + std::string(str.substr(5, 4)) + std::string(str.substr(10, 4));
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

bool matchNumericRange(std::string_view input, std::string_view pattern)
{
    if (pattern.size() < 5 || pattern.front() != '<' || pattern.back() != '>')
        return false;
    std::string_view range = pattern.substr(1, pattern.size() - 1);
    size_t dashPos = range.find('-');
    if (dashPos == std::string_view::npos) return false;
    uint64_t lo = 0, hi = 0;
    if (!stouint(hi, range.data() + dashPos + 1, range.data() + range.size()) ||
        !stouint(lo, range.data(), dashPos))
        return false;
    uint64_t val = 0 ;
    if (!stouint(val, input)) return false;
    return val >= lo && val <= hi;
}

bool isNumericRange(std::string_view p)
{
    static const std::regex pattern(R"(<-?\d+-\-?\d+>)");
    return std::regex_match(p.begin(), p.end(), pattern);
}

bool isIPv4Address(std::string_view address)
{
    static const std::regex pattern(R"(^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$)");
    return std::regex_match(address.begin(), address.end(), pattern);}

bool isIPv6Address(std::string_view address) 
{
    static std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))");
    return std::regex_match(address.begin(), address.end(), ipRegex);
}

bool isIPv6AddressWithMask(std::string_view addressWithMask) 
{
    static std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))/(12[0-8]|1[01][0-9]|[1-9]?[0-9])");
    return std::regex_match(addressWithMask.begin(), addressWithMask.end(), ipRegex);
}

bool isMACAddress(std::string_view macAddress)
{
    static std::regex macRegex(R"(^([0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}|[0-9A-Fa-f]{4}\.[0-9A-Fa-f]{4}\.[0-9A-Fa-f]{4})$)");
    return std::regex_match(macAddress.begin(), macAddress.end(), macRegex);
}

bool isNumber(std::string_view s)
{
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

[[maybe_unused]] std::optional<std::pair<std::string_view, std::string_view>> splitMiddle(std::string_view s, char delim)
{
    auto pos = s.find(delim);
    if (pos == std::string_view::npos) return std::nullopt;
    return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
}
}
