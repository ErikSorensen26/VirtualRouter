// CliUtils.h

#ifndef CLI_UTILS_H
#define CLI_UTILS_H

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

struct IPAddress;
struct IPv4Address;
struct IPv6Address;
struct IPv4Prefix;
struct IPv6Prefix;
struct IPPrefix;

namespace CliUtils
{
bool extractSubnetMask(uint32_t mask, uint8_t& plen);

bool extractIPAddress(const std::string& str, IPAddress& addr);
bool extractIPv4Address(const std::string& str, IPv4Address& addr);
bool extractIPv6Address(const std::string& str, IPv6Address& addr);

bool extractIPPrefix(const std::string& addr, IPPrefix& prefix);
bool extractIPv4Prefix(const std::string& addr, IPv4Prefix& prefix);
bool extractIPv6Prefix(const std::string& addr, IPv6Prefix& prefix);
bool extractIPv4Prefix(const std::string& addr, const std::string& mask, IPPrefix& prefix);
bool extractIPv4Prefix(const std::string& addr, const std::string& mask, IPv4Prefix& prefix);

bool expandIPv6Address(std::string& ipv6Address);
bool extractMacAddress(const std::string& str, uint64_t& mac);

bool isIPv6Address(const std::string& address);
bool isIPv6AddressWithMask(const std::string& addressWithMask);
bool isMACAddress(const std::string& macAddress);
bool isNumber(const std::string& s);
std::optional<std::pair<std::string, std::string>> splitMiddle(const std::string& s, char delim);
}

#endif // CLI_UTILS_H
