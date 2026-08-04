/**
 * @file ExecutorUtils.hpp
 * @brief Token-to-field translation helpers used by the executor.
 * @ingroup CLI_PARSER
 */

#ifndef EXECUTOR_UTILS_HPP
#define EXECUTOR_UTILS_HPP

#include <type_traits>
#include <IPAddress.h>
#include <algorithm>
#include "cli/session/CliUtils.h"
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryDefaultTable.hpp"
#include "configs/RegistryTable.hpp"
#include "cli/modes/Context.hpp"
#include "cli/modes/Mode.hpp"
#include "cli/session/Token.hpp"
#include "configs/FieldAccessor.hpp"

/**
 * @namespace cli::execution::utils
 * @brief Token-to-field translation helpers for CLI command handlers.
 *
 * Provides stateless template utilities that convert raw CLI tokens into
 * typed config field values, and that apply negate/default semantics to
 * registry fields.  Command handler implementations call these instead of
 * parsing tokens directly.
 */
namespace cli::execution::utils
{

/**
 * @brief True when a field's value can be written from a single token.
 *
 * Kind is necessary but not sufficient. The accessor's set() compares old
 * against new to decide whether to fire the live-notification applier, so a
 * value type without operator!= cannot be written at all -- several struct
 * valued fields are in that position. Testing the type here keeps those fields
 * out rather than breaking the build for the whole registry.
 */
template <typename Field>
concept TokenWritable =
    (config::IsAtomicField<Field>
  || config::IsOptionalAtomicField<Field>
  || config::IsValueField<Field>)
    && requires (typename Field::type a) { { a != a } -> std::convertible_to<bool>; };

/**
 * @brief True for keys built from two tokens rather than one.
 *
 * Kept as an explicit list, matching translateDoubleValue's own branches, so the
 * two cannot disagree about which types take the double form -- a key that is
 * double there and single here would parse as a failure rather than as the shape
 * mismatch it is.
 */
template <typename K>
concept DoubleKeyed =
    std::is_same_v<K, interface::InterfaceKey>
 || std::is_same_v<K, types::IPPrefix>
 || std::is_same_v<K, types::IPv4Prefix>;

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
        return cli::utils::stofloat(value, token.value);
    else if constexpr (std::is_same_v<T, types::Mac>)
        return cli::utils::extractMacAddress(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPAddress>)
        return cli::utils::extractIPAddress(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPv4Address>)
        return cli::utils::extractIPv4Address(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPv6Address>)
        return cli::utils::extractIPv6Address(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPPrefix>)
        return cli::utils::extractIPPrefix(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPv4Prefix>)
        return cli::utils::extractIPv4Prefix(token.value, value);
    else if constexpr (std::is_same_v<T, types::IPv6Prefix>)
        return cli::utils::extractIPv6Prefix(token.value, value);
    else if constexpr (std::is_unsigned_v<T>)
    {
        if constexpr (std::is_same_v<uint32_t, T>)
        {
            if (token.pattern == P_IPV4)
            {
                types::IPv4Address addr;
                if (cli::utils::extractIPv4Address(token.value, addr))
                { value = addr.addr; return true; }
                else return false;
            }
        }
        return cli::utils::stouint(value, token.value);
    }
    else if constexpr (std::is_enum_v<T>)
    {
        using Under = std::underlying_type_t<T>;
        Under tmp{};
        if (!cli::utils::stoint(tmp, token.value))
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
        return cli::utils::extractInterfaceId(token1.value, token2.value, value);
    else if constexpr (std::is_same_v<T, types::IPPrefix>)
        return cli::utils::extractIPv4Prefix(token1, token2, value);
    else if constexpr (std::is_same_v<T, types::IPv4Prefix>)
        return cli::utils::extractIPv4Prefix(token1, token2, value);
    return false;
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
requires config::IsAtomicField<typename T::Field> || config::IsOptionalAtomicField<typename T::Field> || config::IsValueField<typename T::Field>
bool handleValueReset(T field, cli::ContextBase& ctx)
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
requires (config::IsAtomicField<typename T::Field> || config::IsOptionalAtomicField<typename T::Field> || config::IsValueField<typename T::Field>)
void setToggleValue(T toggle, cli::ContextBase& ctx)
{
    static_assert(std::is_same_v<bool, typename T::Field::type>,
                  "Field given must hold a boolean type.");
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
 * set, translates the token via @ref translateValue and stores the
 * result with `field.set()`.
 *
 * @tparam T     Field type.
 * @param field  Target registry field.
 * @param ctx    Execution context.
 * @param t      Token pointer; may be null (returns false if translation is needed but token is absent).
 * @return True if the field was updated by any means.
 */
template <typename T>
requires config::IsAtomicField<typename T::Field> || config::IsOptionalAtomicField<typename T::Field> || config::IsValueField<typename T::Field>
bool setFieldValue(T field, cli::ContextBase& ctx, const Token* t)
{
    if (handleValueReset(field, ctx))
        return true;

    using type = typename T::Field::type;

    if (!t) return false;

    type value{};
    if (!translateValue(value, *t))
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
requires config::IsAtomicField<typename T::Field> || config::IsOptionalAtomicField<typename T::Field> || config::IsValueField<typename T::Field>
void setFieldValueWithFallback(T field, cli::ContextBase& ctx, const Token* t)
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
    return translateValue(value, *t);
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
requires config::IsAtomicField<typename T::Field> || config::IsOptionalAtomicField<typename T::Field> || config::IsValueField<typename T::Field>
bool setDoubleFieldValue(T field, cli::ContextBase& ctx, const Token* t1, const Token* t2)
{
    if (handleValueReset(field, ctx))
        return true;
    
    using type = typename T::Field::type;

    if (!t1 || !t2) return false;

    type value{};
    if (!translateDoubleValue(value, *t1, *t2))
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
requires config::IsAtomicField<typename T::Field> || config::IsOptionalAtomicField<typename T::Field> || config::IsValueField<typename T::Field>
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
    return translateDoubleValue(value, *t1, *t2);
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
    return translateValue(field, *t);
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
requires config::IsListField<typename T::Field>
bool setListEntry(T& field, cli::ContextBase& ctx, typename T::Field::node& tup)
{
    using type = typename T::Field::node;

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
requires config::IsOwnedListField<typename T::Field>
void setOwnedField(T& field, cli::ContextBase& ctx, typename T::Field::key& key)
{
    if (ctx.negate || ctx.defaulted)
    {
        field.erase(key);
        return;
    }

    field.emplaceBack(key);
}


/**
 * @brief Builds an owned-list key from the tokens that carry it.
 *
 * Some keys span two tokens -- an InterfaceKey is the type and the number,
 * as in `interface Vlan 10` -- and the single-token translation rejects those
 * outright rather than partially, so the arity is decided from the key type
 * rather than from how many tokens the line happened to carry.
 *
 * The key tokens are the pattern tokens: the field token is the keyword that
 * named the binding, and the values that follow are what identifies which
 * instance of it.
 */
template <typename Key>
bool resolveKey(std::span<Token> toks, Key& key)
{
    Token* last = nullptr;
    Token* prev = nullptr;
    for (auto it = toks.rbegin(); it != toks.rend(); ++it)
    {
        if (it->pattern == P_NONE) continue;
        if (!last)      last = &*it;
        else if (!prev) prev = &*it;
        else            break;
    }

    if (!last) return false;

    if constexpr (DoubleKeyed<Key>)
    {
        if (!prev && last != toks.data())
            prev = last - 1;

        if (!prev) return false;
        return utils::translateDoubleValue(key, *prev, *last);
    }
    else
    {
        return utils::translateValue(key, *last);
    }
}

/**
 * @brief End of the run starting at @p i, given what marks a run's head.
 *
 * A run is its head token plus the arguments trailing it. Arguments are
 * recognised by what they lack: a value the user typed carries no binding of
 * its own, so anything still unbound belongs to the command in front of it.
 * The run ends at the next token that binds something, which is the next
 * command rather than more of this one.
 *
 * Another head of the same kind also ends the run, so two mode changes on one
 * line stay two commands instead of the second's key tokens being read as the
 * first's.
 */
template <typename Pred>
size_t runEnd(std::span<Token> toks, size_t i, Pred head)
{
    for (++i; i < toks.size(); ++i)
        if ((toks[i].*head)() || toks[i].hasNode()) break;

    return i;
}

/**
 * @brief Visits one field of one registry, once the type is known.
 *
 * The half of the lookup that needs a type. ENUM comes from the list walk, so
 * the cast and the visit are ordinary compile-time code by the time they run;
 * the runtime id never appears past this point.
 *
 * A registry with no RegistryOf specialization refuses rather than failing to
 * compile. A templated registry -- PrefixListRegistry<P> is one per family --
 * has no single type for the void* to be cast back to, and it still holds an
 * id and its slots, so the alternative is a hole in a lookup the caller is
 * entitled to make blindly.
 */
template <typename ENUM, typename Fn>
bool visitOne(cli::ContextBase& ctx, uint16_t field, Fn& fn)
{
    if constexpr (config::hasRegistryV<ENUM>)
    {
        // visit indexes the field tuple directly, and the index came from a
        // file, so the bound is checked here rather than trusted.
        if (field >= config::registrySlotsV<ENUM>) return false;

        static_cast<config::RegistryOfT<ENUM>*>(ctx.ctx)->visit(field, fn);
        return true;
    }
    else
    {
        return false;
    }
}

/**
 * @brief Calls @p fn with the accessor for the field @p n binds.
 *
 * The registry comes from the node, not the context: ctx is a bare void* with
 * no tag, and configId already carries the id the tree bound the field
 * through. Recovering the type from that id is the one thing the pointer
 * cannot do for itself -- visit already takes the field index at runtime, so
 * the registry is the only dimension left to resolve.
 *
 * It lives here rather than beside the registry list because the erasure is
 * the CLI's doing. The config layer never loses track of its own types; this
 * only exists to undo what execution did on the way in.
 *
 * @p fn is instantiated once per field of the named registry, so it must
 * compile against all of them -- an `if constexpr` on the field kind is the
 * usual shape, and kinds it does not handle should simply do nothing.
 */
template <typename Fn>
bool visitBound(cli::ContextBase& ctx, const tree::CommandNode& n, Fn&& fn)
{
    if (!ctx.ctx || !n.hasConfig()) return false;

    const uint16_t reg = n.fieldRegistryId();
    if (reg >= config::registryCount) return false;

    const uint16_t field = n.enumIndex();

    bool ok = false;
    config::forEachRegistryId(config::RegistryEntries{}, [&]<typename Entry>()
    {
        using ENUM = typename Entry::type;
        if (config::registryIdV<ENUM> == reg)
            ok = visitOne<ENUM>(ctx, field, fn);
    });

    return ok;
}

/// @brief The mode a mode-change node enters.
inline CliMode modeOf(const tree::CommandNode& n)
{
    return static_cast<CliMode>(n.configExt);
}
}

#endif // EXECUTOR_UTILS_HPP
