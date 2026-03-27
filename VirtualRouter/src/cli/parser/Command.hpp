/**
 * @file Command.hpp
 * @brief Compile-time CLI command descriptor: token matching and handler dispatch.
 */

/**
 * @defgroup CLI CLI
 * @brief Compile-time command parser: Command<>, CliModeParser<>, Executor<>, mode switching.
 */

/**
 * @defgroup CLI_PARSER CLI Parser
 * @ingroup CLI
 * @brief Compile-time command and subcommand descriptors, fixed-string tokens, and mode-parser template.
 */

#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <string>
#include <vector>
#include <type_traits>
#include <cstddef>
#include <iterator>

/**
 * @brief Compile-time CLI command parsing and dispatch.
 *
 * The `cli` namespace owns all types that build the compile-time command
 * parsing pipeline: fixed-string tokens (@ref FixedString), individual
 * command descriptors (@ref Command), prefix-based sub-dispatchers
 * (@ref SubCommand), and the mode-level aggregation type
 * (@ref CliModeParser).
 *
 * None of these types have instances; every method is `static` and the
 * entire dispatch graph is resolved by the compiler at zero runtime cost.
 */
namespace cli
{
/**
 * @brief Tag type that matches any single token in a command pattern.
 * @ingroup CLI
 *
 * Pass `ARG` as a part in a @ref Command to indicate a position that accepts
 * any user-supplied token. The matched token is collected into the `args`
 * vector passed to the handler.
 */
struct ArgTag {};

/// @brief Sentinel value of @ref ArgTag for use in template argument lists.
/// @ingroup CLI
inline constexpr ArgTag ARG{};

/**
 * @brief Tag type that greedily consumes all remaining tokens in a command pattern.
 * @ingroup CLI
 *
 * Exactly one `ARG_REST` may appear in a command pattern; it must be the last
 * logical part. All remaining tokens are collected into the `args` vector.
 * The static_assert inside @ref Command enforces the at-most-one constraint.
 */
struct ArgRestTag {};

/// @brief Sentinel value of @ref ArgRestTag for use in template argument lists.
/// @ingroup CLI
inline constexpr ArgRestTag ARG_REST{};

/**
 * @brief Converts any string-like object that exposes `.data()` and `.size()` to a `std::string_view`.
 * @ingroup CLI
 *
 * @tparam S Type exposing `data()` and `size()` members (e.g. `std::string`, @ref FixedString).
 * @param s Source string-like object.
 * @return Non-owning view over the same character range.
 */
template <typename S>
static inline std::string_view as_sv(const S& s) noexcept
{
    return std::string_view{s.data(), s.size()};
}

/**
 * @brief Compile-time CLI command descriptor binding a token pattern to a handler.
 * @ingroup CLI
 *
 * A `Command` captures one CLI command at compile time as a sequence of
 * "parts": fixed keyword tokens (encoded as @ref FixedString NTTPs),
 * single wildcard slots (@ref ARG), or a trailing greedy capture (@ref ARG_REST).
 *
 * Matching and execution are both zero-allocation paths implemented via C++17
 * fold expressions over the `Parts...` pack.
 *
 * ## Architectural Role
 * `Command` is a pure-static type — it has no data members and no instances.
 * `CliModeParser` folds over a list of `Command` specialisations to produce the
 * full command dispatch for one CLI mode.  The convenience alias @ref commandAdder
 * gives a shorter spelling at definition sites.
 *
 * ## Lifecycle & Ownership
 * No construction or destruction; all methods are `static`.
 *
 * @tparam Context  The context type passed to the handler.  Must derive from
 *                  @ref ContextBase.  All `Command` specializations in one
 *                  @ref CliModeParser must share the same `Context`.
 * @tparam Handler  A non-type template parameter holding a pointer to the
 *                  handler function.  Must be invocable as
 *                  `bool(Context&, const std::vector<std::string>&)`.
 * @tparam Parts    Zero or more compile-time part descriptors: any combination
 *                  of @ref FixedString NTTP values, @ref ARG, or @ref ARG_REST.
 *                  At most one `ARG_REST` is allowed, and it is consumed last.
 *
 * @see CliModeParser
 * @see SubCommand
 * @see commandAdder
 */
template <
    typename Context,
    auto Handler,
    auto... Parts
>
struct Command
{
    using ContextType = Context; ///< Context type shared with all sibling commands.
    static constexpr auto handler = Handler; ///< Handler function pointer.

    static constexpr size_t part_count = sizeof...(Parts); ///< Total number of pattern parts.

    /// True when the pattern contains an @ref ARG_REST part.
    static constexpr bool has_rest_arg =
        (std::is_same_v<std::decay_t<decltype(Parts)>, ArgRestTag> || ...);

    // Only one ARG_REST is allowed
    static_assert(
        ((std::is_same_v<std::decay_t<decltype(Parts)>, ArgRestTag> ? 1 : 0) + ...) <= 1,
        "Only one ARG_REST is allowed in the command pattern."
    );

    static_assert(
        std::is_invocable_v<decltype(Handler), Context&, const std::vector<std::string>&>,
        "Handler must be callable with (Context&, const std::vector<std::string>&)"
    );

    /**
     * @brief Tests whether a token range matches this command's pattern.
     *
     * Iterates over the `Parts` pack and verifies that each fixed token matches
     * its corresponding element in the range.  `ARG` slots always match a
     * present token; `ARG_REST` matches zero or more remaining tokens.
     *
     * @tparam It Forward iterator over string-like elements.
     * @param tokensFirst Start of the token range.
     * @param tokensLast  End of the token range.
     * @return `true` if the range matches this pattern exactly, `false` otherwise.
     */
    template <typename It>
    static bool match(It tokensFirst, It tokensLast) noexcept
    {
        const std::size_t tokenCount =
            static_cast<std::size_t>(std::distance(tokensFirst, tokensLast));

        if constexpr (!has_rest_arg)
        {
            if (tokenCount != part_count)
                return false;
        }
        else
        {
            if (tokenCount < part_count - 1)
                return false;
        }

        bool ok = true;
        size_t idx = 0;

        auto tokenAt = [&](size_t i) -> std::string_view
        {
            auto it = std::next(tokensFirst, static_cast<ptrdiff_t>(i));
            return as_sv(*it);
        };

        auto matchOne = [&](auto part)
        {
            if (!ok) return;

            using PartT = std::decay_t<decltype(part)>;

            if constexpr (std::is_same_v<PartT, ArgTag>)
            {
                if (idx >= tokenCount) { ok = false; return; }
                ++idx;
            }
            else if constexpr (std::is_same_v<PartT, ArgRestTag>)
            {
                idx = tokenCount;
            }
            else
            {
                const std::string_view sv = part.view();
                if (idx >= tokenCount || tokenAt(idx) != sv)
                {
                    ok = false;
                    return;
                }
                ++idx;
            }
        };

        (matchOne(Parts), ...);

        return ok;
    }

    /**
     * @brief Convenience overload accepting a token vector directly.
     *
     * @param tokens Pre-split command tokens.
     * @return `true` if the vector matches this pattern.
     */
    static bool match(const std::vector<std::string>& tokens) noexcept
    {
        return match(tokens.begin(), tokens.end());
    }

    /**
     * @brief Matches and, on success, dispatches to the handler.
     *
     * Collects `ARG` and `ARG_REST` tokens into a temporary `args` vector and
     * calls `Handler(ctx, args)` only if the pattern matches.  Fixed tokens are
     * not included in `args`.
     *
     * @tparam It Forward iterator over string-like elements.
     * @param ctx       Mutable reference to the current execution context.
     * @param tokensFirst Start of the token range.
     * @param tokensLast  End of the token range.
     * @return `true` if the pattern matched and the handler returned `true`;
     *         `false` if the pattern did not match or the handler returned `false`.
     */
    template <typename It>
    static bool tryExecute(Context& ctx, It tokensFirst, It tokensLast)
    {
        const std::size_t tokenCount =
            static_cast<std::size_t>(std::distance(tokensFirst, tokensLast));

        if constexpr (!has_rest_arg)
        {
            if (tokenCount != part_count)
                return false;
        }
        else
        {
            if (tokenCount < part_count - 1)
                return false;
        }

        std::vector<std::string> args;
        args.reserve(tokenCount);

        bool ok = true;
        std::size_t idx = 0;

        auto tokenAt = [&](std::size_t i) -> std::string_view
        {
            auto it = std::next(tokensFirst, static_cast<std::ptrdiff_t>(i));
            return as_sv(*it);
        };

        auto matchOne = [&](auto part)
        {
            if (!ok) return;

            using PartT = std::decay_t<decltype(part)>;

            if constexpr (std::is_same_v<PartT, ArgTag>)
            {
                if (idx >= tokenCount)
                {
                    ok = false;
                    return;
                }

                args.emplace_back(tokenAt(idx));
                ++idx;
            }
            else if constexpr (std::is_same_v<PartT, ArgRestTag>)
            {
                while (idx < tokenCount)
                {
                    args.emplace_back(tokenAt(idx));
                    ++idx;
                }
            }
            else
            {
                auto sv = part.view();
                if (idx >= tokenCount || tokenAt(idx) != sv)
                {
                    ok = false;
                    return;
                }
                ++idx;
            }
        };

        (matchOne(Parts), ...);

        if (!ok)
            return false;

        return Handler(ctx, args);
    }

    /**
     * @brief Convenience overload accepting a token vector directly.
     *
     * @param ctx    Mutable reference to the current execution context.
     * @param tokens Pre-split command tokens.
     * @return Result of the iterator-based overload.
     */
    static bool tryExecute(Context& ctx, const std::vector<std::string>& tokens)
    {
        return tryExecute(ctx, tokens.begin(), tokens.end());
    }
};

/**
 * @brief Convenience alias for @ref Command used at command-definition sites.
 * @ingroup CLI
 *
 * Provides a slightly more readable name at the point where individual commands
 * are declared inside a parser header.
 *
 * @tparam Context Same as @ref Command::Context.
 * @tparam Handler Same as @ref Command::Handler.
 * @tparam Parts   Same as @ref Command::Parts.
 */
template <
    typename Context,
    auto Handler,
    auto... Parts
>
using commandAdder = Command<Context, Handler, Parts...>;
}

#endif // COMMAND_HPP
