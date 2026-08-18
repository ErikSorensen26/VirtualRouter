/**
 * @file ValueFormat.hpp
 * @brief Turns a stored field value back into the token(s) the grammar reads it from.
 * @ingroup CONFIG_SERIALIZER
 *
 * Parsing this codebase has always been one-way -- @c CliUtils and
 * @c ExecutorUtils turn CLI text into a typed value, and nothing turns a typed
 * value back into text, because nothing needed to until now. This is that
 * other direction, kept to formatting alone: it knows nothing about fields,
 * registries or the command tree, only how to spell one value.
 *
 * Overload on the value's type rather than dispatch through the field kind,
 * so a new scalar type is supported by adding one overload here rather than
 * touching the walker in ConfigSerializer.hpp.
 */

#if 0
#ifndef CONFIG_SERIALIZER_VALUE_FORMAT_HPP
#define CONFIG_SERIALIZER_VALUE_FORMAT_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "IPAddress.h"
#include "configs/EnumSchema.hpp"
#include "interface/configs/InterfaceType.hpp"

namespace config::serializer
{
/// @brief Formats a plain integer as decimal text.
template <typename T>
requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
std::string formatValue(T v)
{
    if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
        return std::to_string(static_cast<int>(v));
    else
        return std::to_string(v);
}

/**
 * @brief A bool field is a bare toggle keyword, not "true"/"false"; the caller decides the keyword.
 *
 * Constrained to an exact @c bool argument rather than a plain @c formatValue(bool)
 * overload: a non-template overload is a candidate for anything implicitly
 * convertible to bool, which includes any type with a one-step conversion to
 * an integral (IPv6Address's `operator __uint128_t()`, for one) and made an
 * otherwise-unambiguous call to a real overload ambiguous against this one.
 * An exact-match constraint only competes for a literal bool.
 */
template <typename T>
requires std::is_same_v<T, bool>
std::string formatValue(T) = delete;

inline std::string formatValue(const std::string& v)
{
    return v;
}

inline std::string formatValue(std::string_view v)
{
    return std::string(v);
}

inline std::string formatValue(const types::IPv4Address& v)
{
    const uint32_t a = static_cast<uint32_t>(v);
    return std::to_string((a >> 24) & 0xFF) + "." + std::to_string((a >> 16) & 0xFF) + "." +
           std::to_string((a >> 8) & 0xFF) + "." + std::to_string(a & 0xFF);
}

/// @brief Dual-stack address: dispatches on isIPv4() the same way formatValue(IPPrefix) does.
inline std::string formatValue(const types::IPAddress& v)
{
    return v.isIPv4() ? formatValue(types::IPv4Address(v.v4())) : formatValue(types::IPv6Address(v.v6()));
}

inline std::string formatValue(const types::IPv4Prefix& v)
{
    return formatValue(types::IPv4Address(static_cast<uint32_t>(v))) + "/" +
           std::to_string(static_cast<unsigned>(v.prefixLength));
}

/// @brief Colon-hex form; not the shortened "::" form, which is a later refinement.
inline std::string formatValue(const types::IPv6Address& v)
{
    const __uint128_t a = v.addr;
    std::string out;
    for (int i = 7; i >= 0; --i)
    {
        uint16_t group = static_cast<uint16_t>((a >> (i * 16)) & 0xFFFF);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%x", group);
        out += buf;
        if (i != 0) out += ':';
    }
    return out;
}

inline std::string formatValue(const types::IPv6Prefix& v)
{
    return formatValue(types::IPv6Address(v.addr)) + "/" + std::to_string(static_cast<unsigned>(v.prefixLength));
}

inline std::string formatValue(const types::IPPrefix& v)
{
    return v.isIPv4() ? formatValue(types::IPv4Prefix(v)) : formatValue(types::IPv6Prefix(v));
}

/**
 * @brief Formats an interface key as its two CLI tokens, joined by one space.
 *
 * The grammar reads an interface name as two tokens -- the type keyword and
 * the number ("GigabitEthernet", "0.1") -- via extractInterfaceId(), not one
 * concatenated word; the caller places this wherever those two tokens belong
 * in its line rather than assuming they are the whole of it.
 *
 * The number is fixed-point at 1/256 (see encodeInterfaceKey); printed with
 * trailing zeros trimmed so a whole-number interface reads "1", not
 * "1.000000", and a sub-interface reads "1.1" rather than "1.100000".
 */
inline std::string formatValue(const interface::InterfaceKey& v)
{
    auto [type, id] = v.decode();
    std::string out = interface::getInterfaceType(type);
    out.push_back(' ');

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(id));
    std::string_view digits(buf);
    size_t end = digits.find_last_not_of('0');
    if (end != std::string_view::npos && digits[end] == '.') --end;
    out += digits.substr(0, end + 1);
    return out;
}

/**
 * @brief Formats a config enum's member name, e.g. `AreaType::STUB` -> "stub".
 *
 * The grammar spells enum members as the lowercase keyword it matched, and the
 * table @ref DEFINE_CONFIG_ENUM built stores names exactly as written in the
 * member-list macro (upper case, by this codebase's convention) -- so this
 * lowercases what the table returns rather than assuming the table already
 * has CLI casing, which would silently drift if that convention ever changed.
 */
template <typename E>
requires std::is_enum_v<E> && config::hasEnumSchemaV<E>
std::string formatValue(E v)
{
    std::string_view name = config::enumMemberName<E>(static_cast<uint16_t>(v));
    std::string out(name);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
}

#endif // CONFIG_SERIALIZER_VALUE_FORMAT_HPP
#endif
