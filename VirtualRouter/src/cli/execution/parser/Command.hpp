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

#include <vector>
#include <span>
#include <type_traits>
#include <cstddef>
#include "cli/modes/contexts/Context.hpp"
#include "cli/execution/parser/Token.hpp"

/**
 * @brief Compile-time CLI command parsing and dispatch.
 *
 * The `cli` namespace owns all types that build the compile-time command
 * parsing pipeline: individual command descriptors (@ref Command),
 * prefix-based sub-dispatchers (@ref SubCommand), and the mode-level
 * aggregation type (@ref CliModeParser).
 *
 * None of these types have instances; every method is `static` and the
 * entire dispatch graph is resolved by the compiler at zero runtime cost.
 */
namespace cli::execution
{
/**
 * @brief Extracts the `ContextType` from a handler function pointer type.
 * @ingroup CLI_PARSER
 *
 * Primary template is declared but not defined; only the specialization for
 * the expected handler signature is provided.  A static_assert in @ref Command
 * uses this to enforce the correct signature at the point of use.
 *
 * @tparam T  Handler function pointer type to inspect.
 */
template <typename T>
struct HandlerTraits;

/// @brief Specialization for the canonical handler signature `bool(Context<C>&, ...)`.
template <typename C>
struct HandlerTraits<bool(*)(Context<C>&, const std::vector<std::span<Token>>&)>
{
    using ContextType = C; ///< The SubRegistry type the handler's context is parameterized on.
};

/**
 * @brief Compile-time CLI command descriptor binding a token pattern to a handler.
 * @ingroup CLI
 *
 * A `Command` captures one CLI command at compile time as a sequence of
 * fixed keyword hashes and `ARG` wildcards.  Matching is done against a
 * flat token span starting at a caller-supplied offset, via @ref segmentTokens:
 * each segment's `[0]` token carries the keyword, and `[1+]` carry any
 * user-supplied pattern values that follow it.
 *
 * The handler receives the full `std::vector<std::span<Token>>` so it can
 * inspect both keywords and pattern values at any position.
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
 *                  `bool(Context&, const std::vector<std::span<Token>>&)`.
 * @tparam Parts    Zero or more compile-time token hashes (`_tok` literals or `ARG`)
 *                  describing the expected tokens starting at the match offset.
 *                  Keyword hashes must match exactly; `ARG` accepts any pattern token.
 *
 * @see CliModeParser
 * @see SubCommand
 * @see commandAdder
 */
template <
    auto Handler,
    uint64_t... Parts
>
    requires requires { typename HandlerTraits<decltype(Handler)>::ContextType; }
struct Command
{
    using ContextType = typename HandlerTraits<decltype(Handler)>::ContextType; ///< Context type shared with all sibling commands.
    static constexpr auto handler = Handler; ///< Handler function pointer.
    static constexpr size_t partCount = sizeof...(Parts); ///< Number of fixed keyword parts.
    static constexpr std::array<uint64_t, partCount> parts = { Parts... };

    static_assert(
        std::is_same_v<bool, std::invoke_result_t<decltype(Handler), Context<ContextType>&, const std::vector<std::span<Token>>&>>,
        "Handler must return bool"
    );

    /**
     * @brief Matches tokens starting at `idx` and dispatches the handler with all segments.
     * @ingroup CLI_PARSER
     *
     * Checks whether `tokens[idx..idx+partCount)` match `parts`: keyword slots compare
     * by hash, `ARG` slots accept any pattern token.  On success, `segmentTokens` is
     * called on the full span (so the handler can see every keyword and its arguments,
     * including any prefix tokens consumed by a parent `SubCommand`), and the handler
     * is invoked with the resulting segment vector.
     *
     * Matching succeeds only if:
     *   1. `tokens.size() - idx >= partCount`.
     *   2. Each non-ARG part matches the hash of the corresponding token.
     *   3. Each ARG part corresponds to a pattern token.
     *
     * @param ctx    Mutable reference to the execution context.
     * @param tokens Full flat token span for the current command line.
     * @param idx    Offset into `tokens` at which matching begins (advanced by parent SubCommands).
     * @return `true` if the pattern matched and the handler returned `true`.
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

        std::vector<std::span<Token>> segs = segmentTokens(tokens, idx);
        return Handler(ctx, segs);
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
    auto Handler,
    auto... Parts
>
using commandAdder = Command<Handler, Parts...>;
}

#endif // COMMAND_HPP
