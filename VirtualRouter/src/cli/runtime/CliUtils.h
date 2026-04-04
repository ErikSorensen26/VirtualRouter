/**
 * @file CliUtils.h
 * @brief CLI input validation and network address extraction utilities.
 */

#ifndef CLI_UTILS_H
#define CLI_UTILS_H

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <charconv>
#include <optional>
#include <string>
#include <utility>
#include <algorithm>
#include <Mac.hpp>
#include "interface/configs/InterfaceType.hpp"
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryDefaultTable.hpp"
#include "cli/modes/contexts/ContextBase.hpp"
#include "Token.hpp"

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
template <typename T>
requires std::is_unsigned_v<T> && (sizeof(T) <= 8)
bool stouint(T& val, std::string_view sv)
{
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec == std::errc{}) return true;
    return false;
}

template <typename T>
requires std::is_unsigned_v<T> && (sizeof(T) <= 8)
bool stouint(T& val, const char* sv, size_t siz)
{
    auto [ptr, ec] = std::from_chars(sv, sv + siz, val);
    if (ec == std::errc{}) return true;
    return false;
}

template <typename T>
requires std::is_unsigned_v<T> && (sizeof(T) <= 8)
bool stouint(T& val, const char* sv1, const char* sv2)
{
    auto [ptr, ec] = std::from_chars(sv1, sv2, val);
    if (ec == std::errc{}) return true;
    return false;
}

template <typename T>
requires std::is_floating_point_v<T>
bool stofloat(T& val, std::string_view sv)
{
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    return ec == std::errc{};
}

template <typename T>
bool translateValue(T& value, const Token& token)
{
    if constexpr (config::IsIgnoreCompare<T>)
        return translateValue(value.value, token);
    else if constexpr (std::is_unsigned_v<T>)
        return stouint(value, token.value);
    else if constexpr (std::is_floating_point_v<T>)
        return stofloat(value, token.value);
    else if constexpr (std::is_same_v<T, types::Mac>)
        return extractMacAddress(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPAddress>)
        return extractIPAddress(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPv4Address>)
        return extractIPv4Address(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPv6Address>)
        return extractIPv6Address(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPPrefix>)
        return extractIPPrefix(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPv4Prefix>)
        return extractIPv4Prefix(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPv6Prefix>)
        return extractIPv6Prefix(token.value, value);
    else if constexpr (std::is_same_v<T, std::string>)
    {
        value = token.value; 
        return true;
    }
    return false;
}

template <typename T>
bool translateDoubleValue(T& value, const Token& token1, const Token& token2)
{
    if constexpr (std::is_same_v<T, interface::InterfaceKey>)
        return extractInterfaceId(token1.value, token2.value, value);
    else if constexpr (std::is_same_v<T, types::IPPrefix>)
        return extractIPv4Prefix(token1, token2, value);
    else if constexpr (std::is_same_v<T, types::IPv4Prefix>)
        return extractIPv4Prefix(token1, token2, value);
    return false;
} 

template <typename T>
requires config::IsAtomicField<T> || config::IsOptionalAtomicField<T> || config::IsValueField<T>
bool handleValueReset(T& field, cli::ContextBase& ctx)
{
    if (ctx.negate)
    {
        field.unset();
        return true;
    }
    else if (ctx.defaulted)
    {
        field.setDefault();
        return true;
    }
    return false;
}

template <typename T>
requires config::IsAtomicField<T> || config::IsOptionalAtomicField<T> || config::IsValueField<T>
inline bool setFieldValue(T& field, cli::ContextBase& ctx, Token* t = nullptr)
{
    if (handleValueReset(field, ctx))
        return true;

    using type = config::DefType<T>::type;

    if constexpr (std::is_same_v<type, bool> && config::IsAtomicField<T>)
    {
        field.set(!field.getDefault());
        return true;
    }
    else
    {
        if (!t) return false;

        T value{};
        if (!translateValue(value, *t))
            return false;

        field.set(value);
        return true;
    }
}

template <typename T>
requires config::IsAtomicField<T> || config::IsOptionalAtomicField<T> || config::IsValueField<T>
inline bool setDoubleFieldValue(T& field, cli::ContextBase& ctx, Token* t1, Token* t2)
{
    if (handleValueReset(field, ctx))
        return true;

    using type = config::DefType<T>::type;
    if constexpr (std::is_same_v<type, bool> && config::IsAtomicField<T>)
    {
        field.set(!field.getDefault());
        return true;
    }
    else
    {
        if (!t1 || !t2) return false;

        T value{};
        if (!translateDoubleValue(value, *t1, *t2))
            return false;

        field.set(value);
        return true;
    }
}

template <typename T>
inline bool setTupleElement(T& field, Token* t = nullptr)
{
    if (!t) return false;
    return translateValue(field, *t);
}

template <typename T>
inline bool setDoubleTupleElement(T& field, Token* t1, Token* t2)
{
    if (!t1 || !t2) return false;
    return translateDoubleValue(field, *t1, *t2);
}

template <typename T>
struct isOptional : std::false_type {};

template <typename T>
struct isOptional<std::optional<T>> : std::true_type {};

template <size_t I = 0, typename Tuple>
bool compareTuple(const Tuple& lhs, const Tuple& rhs)
{
    if constexpr (I < std::tuple_size_v<Tuple>)
    {
        using Elem = std::tuple_element_t<I, Tuple>;

        if constexpr (isOptional<Elem>::value)
        {
            const auto& lhsOpt = std::get<I>(lhs);
            const auto& rhsOpt = std::get<I>(rhs);

            if (rhsOpt.has_value() && lhsOpt != rhsOpt)
                return false;
        }
        else if constexpr (!config::IsIgnoreCompare<Elem>)
        {
            if (std::get<I>(lhs) != std::get<I>(rhs))
                return false;
        }

        return compareTuple<I + 1>(lhs, rhs);
    }

    return true;
}

template <typename T>
struct IsTuple : std::false_type {};

template <typename... Ts>
struct IsTuple<std::tuple<Ts...>> : std::true_type {};

template <typename T>
requires config::IsListField<T>
inline bool addListEntry(T& field, cli::ContextBase& ctx, typename config::DefType<T>::type& tup)
{
    using type = config::DefType<T>::type;

    if (ctx.negate || ctx.defaulted)
    {
        field.withWrite([&](std::vector<type>& entries) {
            std::erase_if(entries, [&](const type& entry) {
                if constexpr (IsTuple<T>::value)
                    return compareTuple(entry, tup);
                else
                    return entry == tup;
            });
        });
        return true;
    }

    field.withWrite([&](std::vector<type>& list) {
        auto it = std::find_if(list.begin(), list.end(), [&](const type& entry) {
            if constexpr (IsTuple<T>::value)
                return compareTuple(entry, tup);
            else
                return entry == tup;
        });

        if (it != list.end())
            *it = tup;
        else
            list.push_back(tup);
    });

    return true;
}

/**
 * @brief Converts the tokens and sets the interface id.
 *
 * @param iface reference to interface id to set.
 * @param type  token pointer for interface type.
 * @param id    token pointer for interface number.
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
bool extractMacAddress(std::string_view str, types::Mac mac);

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

bool isNumericRange(std::string_view p);
bool isIPv4Address(std::string_view address);
bool isIPv6Address(std::string_view address);
bool isIPv6AddressWithMask(std::string_view addressWithMask);
bool isMACAddress(std::string_view macAddress);
bool isNumber(std::string_view s);

/**
 * @brief Splits a string into two halves at the first occurrence of `delim`.
 *
 * @param s      String to split.
 * @param delim  Delimiter character.
 * @return A pair `{left, right}` if `delim` was found, or `std::nullopt`.
 */
std::optional<std::pair<std::string_view, std::string_view>> splitMiddle(std::string_view s, char delim);
}

#endif // CLI_UTILS_H
