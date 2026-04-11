/**
 * @file CommandUtils.hpp
 * @brief Token-to-field translation helpers used by CLI command handlers.
 * @ingroup CLI_PARSER
 */

#ifndef COMMAND_UTILS_HPP
#define COMMAND_UTILS_HPP

#include <type_traits>
#include <bitset>
#include <IPAddress.h>
#include <algorithm>
#include "cli/runtime/CliUtils.h"
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryDefaultTable.hpp"
#include "cli/modes/contexts/Context.hpp"
#include "cli/runtime/Token.hpp"

/**
 * @namespace cli::utils
 * @brief Token-to-field translation helpers for CLI command handlers.
 *
 * Provides stateless template utilities that convert raw CLI tokens into
 * typed config field values, and that apply negate/default semantics to
 * registry fields.  Command handler implementations call these instead of
 * parsing tokens directly.
 */
namespace cli::utils
{
namespace detail
{
/**
 * @brief Converts a single token's string value into the destination type `T`.
 *
 * Dispatch is fully compile-time: each `if constexpr` branch handles a
 * specific type family (bool, float, MAC, IP address types, unsigned integers,
 * enums, and strings). Returns false if the token cannot be interpreted as
 * the requested type.
 *
 * @tparam T     Destination value type; deduced from the `value` argument.
 * @param value  Output parameter populated on success.
 * @param token  Source CLI token (carries both raw string and pattern tag).
 * @return True if translation succeeded, false if the token was invalid for `T`.
 */
template <typename T>
bool translateValue(T& value, const Token& token)
{
    if constexpr (config::IsIgnoreCompare<T>)
        return translateValue(value.value, token);
    else if constexpr (std::is_same_v<T, bool>)
        return true;
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
    else if constexpr (std::is_unsigned_v<T>)
    {
        if constexpr (std::is_same_v<uint32_t, T>)
        {
            if (token.pattern == P_IPV4)
            {
                types::IPv4Address addr;
                if (extractIPv4Address(token.value, addr))
                { value = addr.addr; return true; }
                else return false;
            }
        }
        return stouint(value, token.value);
    }
    else if constexpr (std::is_enum_v<T>)
    {
        using Under = std::underlying_type_t<T>;
        Under tmp{};
        if (!stoint(tmp, token.value))
            return false;
        value = static_cast<T>(tmp);
        return true;
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        value = token.value; 
        return true;
    }
    return false;
}

/**
 * @brief Converts two consecutive tokens into a composite destination type `T`.
 *
 * Used where a single config value requires two tokens on the command line,
 * such as an interface key (type + number) or an address+mask pair.
 *
 * @tparam T      Destination composite type.
 * @param value   Output parameter populated on success.
 * @param token1  First CLI token.
 * @param token2  Second CLI token.
 * @return True if both tokens could be combined into the requested type.
 */
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
}

/**
 * @brief Applies negate or default semantics to a registry field before token translation.
 *
 * If the context's `negate` flag is set, calls `field.unset()`. If `defaulted`
 * is set, calls `field.setDefault()`. Returns true in either case so the caller
 * can skip further token parsing.
 *
 * @tparam T     Field type (AtomicField, OptionalAtomicField, or ValueField).
 * @param field  Registry field to reset.
 * @param ctx    Execution context carrying the negate/default flags.
 * @return True if the field was reset; false if normal token translation should proceed.
 */
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

/**
 * @brief Sets a boolean registry field, respecting negate and default context flags.
 *
 * Negation sets the toggle to `false`; `defaulted` calls `setDefault()`;
 * otherwise sets the toggle to `true`.
 *
 * @tparam T      Boolean field type.
 * @param toggle  Field to modify.
 * @param ctx     Execution context.
 */
template <typename T>
requires ((config::IsAtomicField<T> || config::IsOptionalAtomicField<T> || config::IsValueField<T>)
          && std::is_same_v<bool, typename config::DefType<T>::type>)
void setToggleValue(T& toggle, cli::ContextBase& ctx)
{
    if (ctx.negate)
    {
        toggle.set(false);
        return;
    }
    else if (ctx.defaulted)
    {
        toggle.setDefault();
        return;
    }

    toggle.set(true);
}

/**
 * @brief Translates a single token into a registry field value.
 *
 * Checks negate/default flags first via @ref handleValueReset. If neither is
 * set, translates the token via @ref detail::translateValue and stores the
 * result with `field.set()`.
 *
 * @tparam T     Field type.
 * @param field  Target registry field.
 * @param ctx    Execution context.
 * @param t      Token pointer; may be null (returns false if translation is needed but token is absent).
 * @return True if the field was updated by any means.
 */
template <typename T>
requires config::IsAtomicField<T> || config::IsOptionalAtomicField<T> || config::IsValueField<T>
bool setFieldValue(T& field, cli::ContextBase& ctx, const Token* t)
{
    if (handleValueReset(field, ctx))
        return true;

    using type = config::DefType<T>::type;

    if (!t) return false;

    type value{};
    if (!detail::translateValue(value, *t))
        return false;

    field.set(value);
    return true;
}

/**
 * @brief Translates a token into a registry field, falling back to the default on failure.
 *
 * Calls @ref setFieldValue; if that returns false (token missing or invalid),
 * calls `field.setDefault()` so the field always ends up with a valid value.
 *
 * @tparam T     Field type.
 * @param field  Target registry field.
 * @param ctx    Execution context.
 * @param t      Token pointer; may be null.
 */
template <typename T>
requires config::IsAtomicField<T> || config::IsOptionalAtomicField<T> || config::IsValueField<T>
void setFieldValueWithFallback(T& field, cli::ContextBase& ctx, const Token* t)
{
    if (!setFieldValue(field, ctx, t))
        field.setDefault();
}

/**
 * @brief Translates a single token into a raw (non-field) value.
 *
 * Unlike @ref setFieldValue, this operates on a plain value rather than a
 * registry field and has no negate/default handling.
 *
 * @tparam T     Destination value type.
 * @param value  Output parameter.
 * @param t      Token pointer; returns false if null.
 * @return True if translation succeeded.
 */
template <typename T>
inline bool setValue(T& value, const Token* t)
{
    if (!t) return false;
    return detail::translateValue(value, *t);
}

/**
 * @brief Translates two tokens into a composite registry field value.
 *
 * Used for commands whose single config field requires two CLI tokens (e.g.,
 * interface type + number, address + mask). Checks negate/default flags first.
 *
 * @tparam T     Field type.
 * @param field  Target registry field.
 * @param ctx    Execution context.
 * @param t1     First token pointer; returns false if null.
 * @param t2     Second token pointer; returns false if null.
 * @return True if the field was updated.
 */
template <typename T>
requires config::IsAtomicField<T> || config::IsOptionalAtomicField<T> || config::IsValueField<T>
bool setDoubleFieldValue(T& field, cli::ContextBase& ctx, const Token* t1, const Token* t2)
{
    if (handleValueReset(field, ctx))
        return true;
    
    using type = config::DefType<T>::type;

    if (!t1 || !t2) return false;

    type value{};
    if (!detail::translateDoubleValue(value, *t1, *t2))
        return false;

    field.set(value);
    return true;
}

/**
 * @brief Translates two tokens into a composite registry field, falling back to default on failure.
 *
 * @tparam T     Field type.
 * @param field  Target registry field.
 * @param ctx    Execution context.
 * @param t1     First token pointer.
 * @param t2     Second token pointer.
 */
template <typename T>
requires config::IsAtomicField<T> || config::IsOptionalAtomicField<T> || config::IsValueField<T>
void setDoubleFieldValueWithFallback(T& field, cli::ContextBase& ctx, const Token* t1, const Token* t2)
{
    if (!setDoubleFieldValue(field, ctx, t1, t2))
        field.setDefault();
}

/**
 * @brief Translates two tokens into a raw composite value.
 *
 * @tparam T     Destination composite type.
 * @param value  Output parameter.
 * @param t1     First token pointer; returns false if null.
 * @param t2     Second token pointer; returns false if null.
 * @return True if translation succeeded.
 */
template <typename T>
inline bool setDoubleValue(T& value, const Token* t1, const Token* t2)
{
    if (!t1 || !t2) return false;
    return detail::translateDoubleValue(value, *t1, *t2);
}

/**
 * @brief Translates a single token into one element of a tuple-typed list entry.
 *
 * @tparam T     Element type within the tuple.
 * @param field  Output element reference.
 * @param t      Token pointer; returns false if null.
 * @return True if translation succeeded.
 */
template <typename T>
inline bool setTupleElement(T& field, Token* t = nullptr)
{
    if (!t) return false;
    return detail::translateValue(field, *t);
}

/**
 * @brief Translates two tokens into one composite element of a tuple-typed list entry.
 *
 * @tparam T     Composite element type.
 * @param field  Output element reference.
 * @param t1     First token pointer; returns false if null.
 * @param t2     Second token pointer; returns false if null.
 * @return True if translation succeeded.
 */
template <typename T>
inline bool setDoubleTupleElement(T& field, Token* t1, Token* t2)
{
    if (!t1 || !t2) return false;
    return detail::translateDoubleValue(field, *t1, *t2);
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

/**
 * @brief Inserts, updates, or removes an entry in a list-typed registry field.
 *
 * On negate or default, erases entries matching `tup` by equality (or tuple
 * comparison for tuple node types). Otherwise, updates the first matching
 * entry in place or appends a new one.
 *
 * @tparam T     List field type satisfying `IsListField`.
 * @param field  Target list field.
 * @param ctx    Execution context carrying the negate/default flags.
 * @param tup    Node value to insert, update, or remove.
 * @return Always returns true.
 */
template <typename T>
requires config::IsListField<T>
bool setListEntry(T& field, cli::ContextBase& ctx, typename config::DefType<T>::node& tup)
{
    using type = config::DefType<T>::node;

    if (ctx.negate || ctx.defaulted)
    {
        field.withWrite([&](std::vector<type>& entries) -> bool {
            std::erase_if(entries, [&](const type& entry) {
                if constexpr (IsTuple<type>::value)
                    return compareTuple(entry, tup);
                else
                    return entry == tup;
            });
            return true;
        });
        return true;
    }

    field.withWrite([&](std::vector<type>& list) -> bool {
        auto it = std::find_if(list.begin(), list.end(), [&](const type& entry) {
            if constexpr (IsTuple<type>::value)
                return compareTuple(entry, tup);
            else
                return entry == tup;
        });

        if (it != list.end())
            *it = tup;
        else
            list.push_back(tup);
        return true;
    });

    return true;
}

/**
 * @brief Inserts or removes a keyed entry in an owned-list registry field.
 *
 * On negate or default, erases the entry identified by `key`. Otherwise,
 * calls `emplaceBack` to insert or return the existing entry.
 *
 * @tparam T     Owned list field type satisfying `IsOwnedListField`.
 * @param field  Target owned list field.
 * @param ctx    Execution context.
 * @param key    Key identifying the child entry.
 */
template <typename T>
requires config::IsOwnedListField<T>
void setOwnedField(T& field, cli::ContextBase& ctx, typename config::DefType<T>::key& key)
{
    if (ctx.negate || ctx.defaulted)
    {
        field.erase(key);
        return;
    }

    field.emplaceBack(key);
}

/**
 * @brief Tracks which fields in a fixed set have been set during command parsing.
 *
 * Used in command handlers that accept several mutually-exclusive or
 * partially-exclusive fields on a single command line. After the handler
 * returns, `clearLeft()` unsets any fields in `Es` that the handler did
 * not populate, ensuring the registry never contains a partial write.
 *
 * @tparam T   SubRegistry type owning the fields.
 * @tparam Es  Pack of enum constants identifying the fields to track.
 */
template <typename T, T::type... Es>
requires config::IsSubRegistry<T>
class FieldSetter
{
public:
    static constexpr size_t N = sizeof...(Es);

    bool set(T::type e, Context<T>& ctx, Token* token = nullptr)
    {
        bool isSet = false;
        auto trySet = [&]<T::type E>() {
            if (E == e) isSet = setFieldValue(ctx.configs.template get<E>(), ctx, token);
        };
        (trySet.template operator()<Es>(), ...);
        if (auto idx = indexOf(e); isSet)
            bits.set(*idx);
        return isSet;
    }

    bool set(T::type e, Context<T>& ctx, Token* t1, Token* t2)
    {
        bool isSet = false;
        auto trySet = [&]<T::type E>() {
            if (E == e) isSet = setDoubleFieldValue(ctx.configs.template get<E>(), ctx, t1, t2);
        };
        (trySet.template operator()<Es>(), ...);
        if (auto idx = indexOf(e); isSet)
            bits.set(*idx);
        return isSet;
    }

    void clearLeft(Context<T>& ctx)
    {
        auto tryUnset = [&]<T::type E>() {
            if (auto idx = indexOf(E); idx && bits.test(*idx))
                ctx.configs.template get<E>().unset();
        };
        (tryUnset.template operator()<Es>(), ...);
    }

private:
    static constexpr std::array<typename T::type, N> values = {Es...};
    std::bitset<N> bits{};


    static constexpr std::optional<size_t> indexOf(T::type e)
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (values[i] == e)
                return i;
        }
        return std::nullopt;
    }
};
}

#endif // COMMAND_UTILS_HPP
