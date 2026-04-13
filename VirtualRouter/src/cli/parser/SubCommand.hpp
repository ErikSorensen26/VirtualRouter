/**
 * @file SubCommand.hpp
 * @brief Prefix-stripping delegating command type that forwards to a child parser.
 */

#ifndef SUB_COMMAND_HPP
#define SUB_COMMAND_HPP

#include <cstdint>
#include "cli/runtime/Token.hpp"
#include "cli/modes/contexts/Context.hpp"

/**
 * @brief Compile-time CLI command parsing and dispatch.
 *
 * See Command.hpp for the full namespace description.
 */
namespace cli
{
template <typename T>
struct SubHandlerTraits;

/// @brief Specialization for the canonical handler signature `bool(Context<C>&, ...)`.
template <typename C>
struct SubHandlerTraits<bool(*)(Context<C>&, std::span<Token>, size_t)>
{
    using ContextType = C; ///< The SubRegistry type the handler's context is parameterized on.
};

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
    auto Handler,
    uint64_t... Parts
>
struct SubCommand
{
    using ContextType = typename SubHandlerTraits<decltype(Handler)>::ContextType; ///< Context type shared with all sibling commands.
    static constexpr auto handler = Handler; ///< Handler function pointer.
    static constexpr size_t partCount = sizeof...(Parts); ///< Number of fixed keyword parts.
    static constexpr std::array<uint64_t, partCount> parts = { Parts... };

    static_assert(
        std::is_same_v<bool, std::invoke_result_t<decltype(Handler), Context<ContextType>&, std::span<Token>, std::size_t>>,
        "Handler must return bool"
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
    static bool tryExecute(Context<ContextType>& ctx, std::span<Token> tokens, size_t idx)
    {
        if (tokens.size() - idx < partCount)
            return false;

        for (size_t i = 0; i < partCount; ++i)
        {
            const Token& t = tokens[idx++];

            if (t.isPattern())
            {
                if (parts[i] != P_ARG && parts[i] != static_cast<uint64_t>(t.pattern))
                    return false;
            }
            else
            {
                if (t.hash != parts[i])
                    return false;
            }
        }

        return Handler(ctx, tokens, idx);
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
    auto Handler,
    auto... Parts
>
using subAdder = SubCommand<Handler, Parts...>;
}

#endif // SUB_COMMAND_HPP
