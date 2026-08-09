/**
 * @file CliUtils.h
 * @brief CLI input validation and network address extraction utilities.
 */

#ifndef CLI_UTILS_H
#define CLI_UTILS_H

#include <cstdint>
#include <string_view>
#include <optional>
#include <string>
#include <utility>
#include <Mac.hpp>
#include <charconv>
#include "interface/configs/InterfaceType.hpp"

namespace types { struct IPPrefix; }
namespace types { struct IPv4Address; }
namespace types { struct IPv4Prefix; }
namespace types { struct IPAddress; }
namespace types { struct IPv6Address; }
namespace types { struct IPv6Prefix; }

/**
 * @namespace cli::utils
 * @brief Stateless helpers for parsing and validating CLI token strings.
 *
 * All functions operate on raw string tokens as entered by the user. They have
 * no dependency on session state or the command tree.
 */
namespace cli::utils
{
/**
 * @brief Parses an unsigned integer from a `string_view`.
 *
 * @tparam T   Unsigned integer type (at most 8 bytes).
 * @param val  Output parameter populated on success.
 * @param sv   Source string view.
 * @return True if the entire string was a valid non-negative integer of type `T`.
 */
template <typename T>
requires std::is_unsigned_v<T> && (sizeof(T) <= 8)
bool stouint(T& val, std::string_view sv)
{
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec == std::errc{}) return true;
    return false;
}

/**
 * @brief Parses an unsigned integer from a raw char pointer + length.
 *
 * @tparam T    Unsigned integer type (at most 8 bytes).
 * @param val   Output parameter populated on success.
 * @param sv    Start of the character sequence.
 * @param siz   Length of the character sequence.
 * @return True if parsing succeeded.
 */
template <typename T>
requires std::is_unsigned_v<T> && (sizeof(T) <= 8)
bool stouint(T& val, const char* sv, size_t siz)
{
    auto [ptr, ec] = std::from_chars(sv, sv + siz, val);
    if (ec == std::errc{}) return true;
    return false;
}

/**
 * @brief Parses an unsigned integer from a half-open char range `[sv1, sv2)`.
 *
 * @tparam T    Unsigned integer type (at most 8 bytes).
 * @param val   Output parameter populated on success.
 * @param sv1   Pointer to first character.
 * @param sv2   Pointer one past the last character.
 * @return True if parsing succeeded.
 */
template <typename T>
requires std::is_unsigned_v<T> && (sizeof(T) <= 8)
bool stouint(T& val, const char* sv1, const char* sv2)
{
    auto [ptr, ec] = std::from_chars(sv1, sv2, val);
    if (ec == std::errc{}) return true;
    return false;
}

/**
 * @brief Parses a floating-point number from a `string_view`.
 *
 * @tparam T   Floating-point type.
 * @param val  Output parameter populated on success.
 * @param sv   Source string view.
 * @return True if the entire string was a valid floating-point value.
 */
template <typename T>
requires std::is_floating_point_v<T>
bool stofloat(T& val, std::string_view sv)
{
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    return ec == std::errc{};
}

/**
 * @brief Parses a signed or unsigned integer from a `string_view`.
 *
 * @tparam T   Integral type.
 * @param val  Output parameter populated on success.
 * @param sv   Source string view.
 * @return True if the entire string was a valid integer of type `T`.
 */
template <typename T>
requires std::is_integral_v<T>
bool stoint(T& val, std::string_view sv)
{
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    return ec == std::errc{};
}


/**
 * @brief Converts the tokens and sets the interface id.
 *
 * @param type  token pointer for interface type.
 * @param id    token pointer for interface number.
 * @param[out] key populated with the resolved interface key on success.
 * @return True if the id was successfully calculated, false otherwise.
 */
bool extractInterfaceId(std::string_view type, std::string_view id, interface::InterfaceKey& key);

/**
 * @brief Converts a 32-bit host-byte-order subnet mask to a prefix length.
 *
 * @param[in]  mask  Host-byte-order subnet mask (e.g. 0xFFFFFF00 for /24).
 * @param[out] plen  Populated with the prefix length on success.
 * @return True if the mask is a valid contiguous mask, false otherwise.
 */
bool extractSubnetMask(uint32_t mask, uint8_t& plen);

/**
 * @brief Converts a dotted-decimal mask string to a prefix length.
 *
 * Accepts either spelling: a subnet mask (255.255.255.0) or the wildcard that
 * is its inverse (0.0.0.255). Both name the same prefix, and which one a
 * command takes is a per-protocol convention rather than a difference in
 * meaning -- EIGRP and OSPF `network` statements use the wildcard, while an
 * interface address uses the subnet mask.
 *
 * @param[in]  mask  Dot-decimal mask string, in either spelling.
 * @param[out] plen  Populated with the prefix length on success.
 * @return True if the string parsed and named a contiguous mask.
 */
bool extractMaskLength(std::string_view mask, uint8_t& plen);

/**
 * @brief Parses an IP address string (IPv4 or IPv6) into an @ref types::IPAddress.
 *
 * @param[in]  str   Dot-decimal or colon-hex address string.
 * @param[out] addr  Populated on success.
 * @return True if the string was a valid address of either family.
 */
bool extractIPAddress(std::string_view str, types::IPAddress& addr);

/**
 * @brief Parses a dot-decimal IPv4 address string into an @ref types::IPv4Address.
 *
 * @param[in]  str   Dot-decimal address string (e.g. "192.168.1.1").
 * @param[out] addr  Populated on success.
 * @return True if parsing succeeded.
 */
bool extractIPv4Address(std::string_view str, types::IPv4Address& addr);

/**
 * @brief Parses a colon-hex IPv6 address string into an @ref types::IPv6Address.
 *
 * @param[in]  str   Colon-hex address string, with or without `::` compression.
 * @param[out] addr  Populated on success.
 * @return True if parsing succeeded.
 */
bool extractIPv6Address(std::string_view str, types::IPv6Address& addr);

/**
 * @brief Parses an IP prefix in CIDR notation into an @ref types::IPPrefix.
 *
 * Accepts both IPv4 (e.g. "10.0.0.0/8") and IPv6 (e.g. "2001:db8::/32") CIDR.
 *
 * @param[in]  addr    CIDR prefix string.
 * @param[out] prefix  Populated on success.
 * @return True if parsing succeeded.
 */
bool extractIPPrefix(std::string_view addr, types::IPPrefix& prefix);

/**
 * @brief Parses an IPv4 CIDR prefix string into an @ref types::IPv4Prefix.
 *
 * @param[in]  addr    CIDR string (e.g. "192.168.0.0/16").
 * @param[out] prefix  Populated on success.
 * @return True if parsing succeeded.
 */
bool extractIPv4Prefix(std::string_view addr, types::IPv4Prefix& prefix);

/**
 * @brief Parses an IPv6 CIDR prefix string into an @ref types::IPv6Prefix.
 *
 * @param[in]  addr    CIDR string (e.g. "2001:db8::/48").
 * @param[out] prefix  Populated on success.
 * @return True if parsing succeeded.
 */
bool extractIPv6Prefix(std::string_view addr, types::IPv6Prefix& prefix);

/**
 * @brief Parses an IPv4 address and dotted-decimal mask into an @ref types::IPPrefix.
 *
 * @param[in]  addr    Dot-decimal address string.
 * @param[in]  mask    Dot-decimal subnet mask (e.g. "255.255.0.0").
 * @param[out] prefix  Populated on success.
 * @return True if both the address and mask were valid.
 */
bool extractIPv4Prefix(std::string_view addr, std::string_view mask, types::IPPrefix& prefix);

/**
 * @brief Parses an IPv4 address and dotted-decimal mask into an @ref types::IPv4Prefix.
 *
 * @param[in]  addr    Dot-decimal address string.
 * @param[in]  mask    Dot-decimal subnet mask.
 * @param[out] prefix  Populated on success.
 * @return True if both the address and mask were valid.
 */
bool extractIPv4Prefix(std::string_view addr, std::string_view mask, types::IPv4Prefix& prefix);

/**
 * @brief Expands a compressed IPv6 address string to full eight-group notation in place.
 *
 * @param[in,out] ipv6Address  Address string; replaced with the expanded form on success.
 * @return True if the string was a valid (compressed or full) IPv6 address.
 */
bool expandIPv6Address(std::string& ipv6Address);

/**
 * @brief Parses a MAC address string into a @ref types::Mac value.
 *
 * @param[in]  str  MAC address in any common notation (e.g. "00:1a:2b:3c:4d:5e").
 * @param[out] mac  Populated on success.
 * @return True if parsing succeeded.
 */
bool extractMacAddress(std::string_view str, types::Mac& mac);

/**
 * @brief Tests whether a CLI token satisfies a numeric range pattern.
 *
 * The pattern is a string of the form `<lo-hi>` (e.g. `<0-255>`). Returns
 * true if `input` is a non-negative integer within the closed range [lo, hi].
 *
 * @param input    Token string to test.
 * @param pattern  Range pattern string (e.g. `"<0-4294967295>"`).
 * @return True if `input` is a valid integer within the range.
 */
bool matchNumericRange(std::string_view input, std::string_view pattern);

/// @brief Returns true if `p` matches the `<lo-hi>` numeric range pattern syntax.
bool isNumericRange(std::string_view p);

/// @brief Returns true if `address` is a valid dot-decimal IPv4 address.
bool isIPv4Address(std::string_view address);

/// @brief Returns true if `addressWithMask` is a valid IPv4 CIDR prefix (e.g. `"10.0.0.0/24"`).
bool isIPv4AddressWithMask(std::string_view addressWithMask);

/// @brief Returns true if `address` is a valid colon-hex IPv6 address.
bool isIPv6Address(std::string_view address);

/// @brief Returns true if `addressWithMask` is a valid IPv6 CIDR prefix (e.g. `"2001:db8::/32"`).
bool isIPv6AddressWithMask(std::string_view addressWithMask);

/// @brief Returns true if `macAddress` is a valid MAC address string.
bool isMACAddress(std::string_view macAddress);

/// @brief Returns true if `s` consists entirely of decimal digit characters.
bool isNumber(std::string_view s);

/**
 * @brief Splits a string into two halves at the first occurrence of `delim`.
 *
 * @param s      String to split.
 * @param delim  Delimiter character.
 * @return A pair `{left, right}` if `delim` was found, or `std::nullopt`.
 */
std::optional<std::pair<std::string_view, std::string_view>> splitMiddle(std::string_view s, char delim);

/// @brief Returns a lower-cased copy of `s`.
std::string lowerStr(std::string s);

/// @brief Case-insensitive equality comparison.
bool lowerCmp(std::string_view s1, std::string_view s2);

/// @brief Returns true if `partial` is a case-insensitive prefix of `s2`.
bool partialLowerCmp(std::string_view partial, std::string_view s2);

/// @brief Returns `s` with leading whitespace removed.
std::string trimLeft(const std::string& s);

/// @brief Returns the last whitespace-delimited word of `s`, or empty.
std::string getLastWord(const std::string& s);
}

#endif // CLI_UTILS_H
