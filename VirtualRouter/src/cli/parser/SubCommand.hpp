/**
 * @file SubCommand.hpp
 * @brief Prefix-stripping delegating command type that forwards to a child parser.
 */

#ifndef SUB_COMMAND_HPP
#define SUB_COMMAND_HPP

#include <type_traits>
#include <vector>
#include <string>

#include "Command.hpp"
#include "FixedString.hpp"

/**
 * @brief Compile-time CLI command parsing and dispatch.
 *
 * See Command.hpp for the full namespace description.
 */
namespace cli
{
/**
 * @brief True when `T` is either @ref ArgTag or @ref ArgRestTag.
 * @ingroup CLI_PARSER
 *
 * Used by @ref SubCommand's static_assert to reject wildcard parts in
 * a sub-command prefix, which must consist only of fixed tokens.
 *
 * @tparam T Type to test.
 */
template <typename T>
inline constexpr bool is_arg_tag_v =
    std::is_same_v<std::decay<T>, ArgTag> || std::is_same_v<std::decay<T>, ArgRestTag>;

/**
 * @brief Prefix-based delegating command that forwards remaining tokens to a child parser.
 * @ingroup CLI_PARSER
 *
 * A `SubCommand` consumes a fixed prefix from the token range and, if the
 * prefix matches, hands the remaining tokens to `SubParser::execute()`.  This
 * allows parsers to be composed in a tree: the top-level @ref CliModeParser
 * holds a `SubCommand<Ctx, IPCommands, "ip"_tok>`, which transparently delegates
 * any token stream beginning with `"ip"` to the `IPCommands` sub-parser.
 *
 * ## Architectural Role
 * `SubCommand` is the glue between flat command lists and hierarchical grammar
 * trees.  It provides the same static interface (`match` / `tryExecute`) as
 * @ref Command so that both can appear in a @ref CliModeParser's command pack.
 *
 * ## Lifecycle & Ownership
 * No data members; all methods are `static`.
 *
 * @tparam Context     The shared context type.  Must derive from @ref ContextBase.
 * @tparam SubParser   A @ref CliModeParser specialization that handles the
 *                     tokens after the prefix has been stripped.  Must expose
 *                     the same `ContextType` as `Context`.
 * @tparam PrefixParts Fixed-string tokens that form the required prefix.
 *                     Only @ref FixedString NTTPs are allowed here — no `ARG`
 *                     or `ARG_REST` (enforced by static_assert).
 *
 * @see Command
 * @see CliModeParser
 * @see subAdder
 */
template <
    typename Context,
    typename SubParser,
    FixedString... PrefixParts
>
struct SubCommand
{
    using ContextType = Context; ///< Context type shared with the parent parser.

    static_assert(
        std::is_same_v<typename SubParser::ContextType, Context>,
        "SubCommand: SubParser must use the same Context type"
    );

    // Disallow ARG / ARG_REST in the prefix
    static_assert(
        (!is_arg_tag_v<decltype(PrefixParts)> && ...),
        "SubCommand: prefix cannot include ARG or ARG_REST; use only fixed tokens"
    );

    /**
     * @brief Tests whether a token range begins with the required prefix.
     *
     * Consumes prefix tokens and, if they all match, delegates to
     * `SubParser::match()` with the remaining range.  Returns `true` if the
     * prefix is present and at least one sub-command matches the remainder.
     *
     * @tparam It Forward iterator over string-like elements.
     * @param first Start of the token range.
     * @param last  End of the token range.
     * @return `true` if prefix and sub-parser both match.
     */
    template <typename It>
    static bool match(It first, It last)
    {
        auto it = first;

        bool ok = true;
        auto matchPrefixOne = [&](auto part)
        {
            if (!ok) return;

            if (it == last) { ok = false; return; }

            const std::string_view sv = part.view();
            if (cli::as_sv(*it) != sv) { ok = false; return; }

            ++it;
        };

        (matchPrefixOne(PrefixParts), ...);

        if (!ok)
            return false;

        if (it == last)
            return true;

        return SubParser::match(it, last);
    }

    /**
     * @brief Convenience overload accepting a token vector directly.
     */
    static bool match(const std::vector<std::string>& tokens)
    {
        return match(tokens.begin(), tokens.end());
    }

    /**
     * @brief Strips the prefix and, on success, executes via the sub-parser.
     *
     * If the prefix matches, calls `SubParser::execute(ctx, it, last)` where
     * `it` points to the first token after the prefix.
     *
     * @tparam It Forward iterator over string-like elements.
     * @param ctx   Mutable reference to the current execution context.
     * @param first Start of the token range.
     * @param last  End of the token range.
     * @return `true` if the prefix matched and the sub-parser executed successfully.
     */
    template <typename It>
    static bool tryExecute(Context& ctx, It first, It last)
    {
        auto it = first;

        bool ok = true;
        auto matchPrefixOne = [&](auto part)
        {
            if (!ok) return;

            if (it == last) { ok = false; return; }

            const std::string_view sv = part.view();
            if (cli::as_sv(*it) != sv) { ok = false; return; }

            ++it;
        };

        (matchPrefixOne(PrefixParts), ...);

        if (!ok)
            return false;

        return SubParser::execute(ctx, it, last);
    }

    /**
     * @brief Convenience overload accepting a token vector directly.
     */
    static bool tryExecute(Context& ctx, const std::vector<std::string>& tokens)
    {
        return tryExecute(ctx, tokens.begin(), tokens.end());
    }
};

/**
 * @brief Convenience alias for @ref SubCommand used at definition sites.
 * @ingroup CLI_PARSER
 *
 * @tparam Context   Same as @ref SubCommand::Context.
 * @tparam SubParser Same as @ref SubCommand::SubParser.
 * @tparam Parts     @ref FixedString NTTP values forming the prefix.
 */
template <
    typename Context,
    typename SubParser,
    auto... Parts
>
using subAdder = SubCommand<Context, SubParser, Parts...>;
}

#endif // SUB_COMMAND_HPP
