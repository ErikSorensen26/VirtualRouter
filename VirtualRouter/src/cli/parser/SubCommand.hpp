/**
 * @file SubCommand.hpp
 * @brief Prefix-stripping delegating command type that forwards to a child parser.
 */

#ifndef SUB_COMMAND_HPP
#define SUB_COMMAND_HPP

#include <type_traits>
#include <cstdint>
#include "cli/runtime/Token.hpp"

/**
 * @brief Compile-time CLI command parsing and dispatch.
 *
 * See Command.hpp for the full namespace description.
 */
namespace cli
{

/**
 * @brief Prefix-based delegating command that forwards remaining segments to a child parser.
 * @ingroup CLI_PARSER
 *
 * A `SubCommand` matches a fixed keyword prefix in the flat token span and,
 * on success, forwards the same span to `SubParser::execute()` with an advanced
 * `idx` — so the sub-parser starts matching after the prefix while leaf handlers
 * still receive all segments (including this prefix) via `segmentTokens`.
 * Example: a top-level @ref CliModeParser holding
 * `SubCommand<Ctx, IPCommands, "ip"_tok>` transparently routes any command
 * beginning with `"ip"` into `IPCommands`.
 *
 * ## Architectural Role
 * `SubCommand` is the glue between flat command lists and hierarchical grammar
 * trees.  It provides the same static interface (`tryExecute`) as
 * @ref Command so that both can appear in a @ref CliModeParser's command pack.
 *
 * ## Lifecycle & Ownership
 * No data members; all methods are `static`.
 *
 * @tparam Context     The shared context type.  Must derive from @ref ContextBase.
 * @tparam SubParser   A @ref CliModeParser specialization that handles the
 *                     segments after the prefix has been stripped.  Must expose
 *                     the same `ContextType` as `Context`.
 * @tparam PrefixParts Compile-time keyword hashes (`_tok` literals) forming
 *                     the required prefix.  `ARG` is not valid here — prefix
 *                     routing is keyword-only.
 *
 * @see Command
 * @see CliModeParser
 * @see subAdder
 */
template <
    typename Context,
    typename SubParser,
    uint64_t... PrefixParts
>
struct SubCommand
{
    using ContextType = Context; ///< Context type shared with the parent parser.

    static_assert(
        std::is_same_v<typename SubParser::ContextType, Context>,
        "SubCommand: SubParser must use the same Context type"
    );

    /**
     * @brief Matches the prefix at `idx` and, on success, delegates to the sub-parser.
     *
     * Checks `tokens[idx..idx+prefixSize)` against the compile-time prefix hashes.
     * On match, calls `SubParser::execute` with the same full token span and an
     * advanced `idx` so that leaf `Command` handlers still receive all segments
     * (including this prefix) when they call `segmentTokens`.
     *
     * @param ctx    Mutable reference to the current execution context.
     * @param tokens Full flat token span for the current command line.
     * @param idx    Offset at which prefix matching begins.
     * @return `true` if the prefix matched and the sub-parser executed successfully.
     */
    static bool tryExecute(Context& ctx, std::span<Token> tokens, size_t idx)
    {
        constexpr size_t prefixSize = sizeof...(PrefixParts);
        if (tokens.size() - idx < prefixSize)
            return false;

        constexpr std::array<uint64_t, prefixSize> prefixHashes = { PrefixParts... };

        for (size_t i = 0; i < prefixSize; ++i)
        {
            if (tokens[idx + i].hash != prefixHashes[i])
                return false;
        }

        return SubParser::execute(ctx, tokens, idx + prefixSize);
    }
};

/**
 * @brief Convenience alias for @ref SubCommand used at definition sites.
 * @ingroup CLI_PARSER
 *
 * @tparam Context   Same as @ref SubCommand::Context.
 * @tparam SubParser Same as @ref SubCommand::SubParser.
 * @tparam Parts     Keyword hash NTTPs (`_tok` literals) forming the prefix.
 */
template <
    typename Context,
    typename SubParser,
    auto... Parts
>
using subAdder = SubCommand<Context, SubParser, Parts...>;
}

#endif // SUB_COMMAND_HPP
