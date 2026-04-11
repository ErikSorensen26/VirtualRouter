/**
 * @file CliModeParser.hpp
 * @brief Mode-bound command dispatcher and `operator""_tok` literal.
 */

#ifndef CLI_MODE_PARSER_HPP
#define CLI_MODE_PARSER_HPP

#include <type_traits>
#include "cli/modes/contexts/Context.hpp"

#include "cli/parser/Command.hpp" // IWYU pragma: keep
#include "cli/parser/SubCommand.hpp" // IWYU pragma: keep

/**
 * @brief Compile-time CLI command parsing and dispatch.
 *
 * See Command.hpp for the full namespace description.
 */
namespace cli
{
/**
 * @brief User-defined string literal for creating compile-time hashed tokens.
 * @ingroup CLI_PARSER
 *
 * This literal generates a `FixedToken` value (as a `uint64_t` hash) from a
 * string, intended for use as a non-type template parameter (NTTP) in
 * `Command` or `SubCommand` definitions. It allows embedding fixed keyword
 * tokens in a clear, readable syntax:
 *
 * @code
 *   using MyCmd = commandAdder<Ctx, handler, "show"_tok, "version"_tok>;
 * @endcode
 *
 * The resulting value is computed at compile-time and can be used anywhere a
 * constant expression is required.
 *
 * @param str  Null-terminated C string representing the token.
 * @param len  Length of the string (excluding null terminator).
 * @return     Compile-time hashed value of the string (`uint64_t`), suitable
 *             as a NTTP for templates like `commandAdder`.
 */
template <typename C, typename... Commands>
class CliModeParser;

// TRAITS

/**
 * @brief Convenience variable template.
 * @ingroup CLI_PARSER
 */
template <typename T>
concept is_cli_mode_v = std::derived_from<T, CliModeParserFlag>;

/**
 * @brief Groups a set of commands under a single CLI mode and provides a unified execute interface.
 * @ingroup CLI_PARSER
 *
 * `CliModeParser` is the primary building block for the CLI dispatch layer.
 * It ties a @ref CliMode enum value to a @ref Context type and a variadic
 * pack of @ref Command and @ref SubCommand (or nested `CliModeParser`) types.
 *
 * Dispatching is a linear fold over the `Commands` pack; the first command
 * whose pattern matches is executed and the fold stops.
 *
 * ## Architectural Role
 * - One `CliModeParser` specialization exists per CLI mode (e.g. `GlobalCommands`,
 *   `InterfaceCommands`, `RouterEigrpClassicCommandsV4`).
 * - They are collected into the @ref Executor's template argument list through
 *   the @ref ExecutionManager type alias.
 * - Nested sub-parsers (via @ref SubCommand) can be embedded in the `Commands`
 *   pack; `is_cli_mode_v` distinguishes them from flat @ref Command types.
 *
 * ## Lifecycle & Ownership
 * No instances; all methods are `static`.
 *
 * @tparam Mode     The @ref CliMode this parser owns.  Must be unique within
 *                  a given @ref Executor instantiation.
 * @tparam Context  Shared context type.  All entries in `Commands` must expose
 *                  the same `ContextType` (enforced by static_assert).
 * @tparam Commands Zero or more @ref Command, @ref SubCommand, or nested
 *                  `CliModeParser` types, all sharing `Context`.
 *
 * @see Command
 * @see SubCommand
 * @see Executor
 */
template <typename C, typename... Commands>
class CliModeParser
{
    static_assert((std::is_same_v<C, typename Commands::ContextType> && ...),
                  "All Commands must share the same Context type");

public:
    using ContextType = C; ///< Context type for this mode.

    /**
     * @brief Attempts to execute the first matching command in the pack.
     *
     * Folds over `Commands...` and calls `tryExecute` on each `Command` or
     * `SubCommand`, or `execute` on nested `CliModeParser` types, until one
     * succeeds.  Dispatch stops at the first match.
     *
     * @param ctx    Mutable execution context.
     * @param tokens Full flat token span for the current command line.
     * @param idx    Offset at which this parser should begin matching.
     *               Defaults to 0; advanced by parent `SubCommand` for nested parsers.
     * @return `true` if a command matched and executed successfully.
     */
    static bool execute(Context<C>& ctx, std::span<Token> tokens, size_t idx = 0)
    {
        bool executed = false;

        auto tryOne = [&](auto cmdType)
        {
            using CmdT = decltype(cmdType);

            if (executed)
                return;

            if constexpr (is_cli_mode_v<CmdT>)
            {
                if (CmdT::execute(ctx, tokens, idx))
                    executed = true;
            }
            else
            {
                if (CmdT::tryExecute(ctx, tokens, idx))
                    executed = true;
            }
        };

        (tryOne(Commands{}), ...);

        return executed;
    }
};
}

#endif // CLI_MODE_PARSER_HPP
