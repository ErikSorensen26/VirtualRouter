/**
 * @file CliModeParser.hpp
 * @brief Mode-bound command dispatcher and `operator""_tok` literal.
 */

#ifndef CLI_MODE_PARSER_HPP
#define CLI_MODE_PARSER_HPP

#include <string>
#include <vector>
#include <type_traits>
#include <json.hpp>
#include <memory_resource>

#include "FixedString.hpp"
#include "cli/modes/Mode.hpp"

#define UNUSED(x) (void)(x)

/**
 * @brief Compile-time CLI command parsing and dispatch.
 *
 * See Command.hpp for the full namespace description.
 */
namespace cli
{
/**
 * @brief User-defined string literal that produces a @ref FixedString NTTP.
 * @ingroup CLI_PARSER
 *
 * Used at @ref Command and @ref SubCommand definition sites to embed fixed
 * keyword tokens in a readable way:
 * @code
 *   using MyCmd = commandAdder<Ctx, handler, "show"_tok, "version"_tok>;
 * @endcode
 *
 * @tparam S  The string literal deduced at compile time.
 * @return    A `FixedString<N>` copy of the literal, suitable as an NTTP.
 */
template <FixedString S>
consteval auto operator""_tok()
{
    return S;
}

template <CliMode Mode, typename Context, typename... Commands>
class CliModeParser;

// TRAITS

/**
 * @brief Primary template: `T` is not a @ref CliModeParser.
 * @ingroup CLI_PARSER
 *
 * Used by @ref Executor to distinguish between raw @ref Command types and
 * nested @ref CliModeParser types inside a command pack.
 *
 * @tparam T Type to inspect.
 */
template <typename T, typename = void> struct is_cli_mode : std::false_type {};

/**
 * @brief Partial specialization: `T` is a @ref CliModeParser (has a `mode` member).
 * @ingroup CLI_PARSER
 */
template <typename T>
struct is_cli_mode<T, std::void_t<decltype(T::mode)>> : std::true_type {};

/**
 * @brief Convenience variable template for @ref is_cli_mode.
 * @ingroup CLI_PARSER
 */
template <typename T>
inline constexpr bool is_cli_mode_v = is_cli_mode<std::decay_t<T>>::value;

/**
 * @brief Groups a set of commands under a single CLI mode and provides a unified execute/match interface.
 * @ingroup CLI_PARSER
 *
 * `CliModeParser` is the primary building block for the CLI dispatch layer.
 * It ties a @ref CliMode enum value to a @ref Context type and a variadic
 * pack of @ref Command and @ref SubCommand (or nested `CliModeParser`) types.
 *
 * Dispatching is a linear fold over the `Commands` pack; the first command
 * whose pattern matches is executed and the fold stops.
 *
 * `addSupport()` walks a JSON command-tree and marks which entries are
 * implemented, enabling the web console to render support status.
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
template <CliMode Mode, typename Context, typename... Commands>
class CliModeParser
{
    static_assert((std::is_same_v<Context, typename Commands::ContextType> && ...),
                  "All Commands must share the same Context type");

public:
    using ContextType = Context; ///< Context type for this mode.
    static constexpr CliMode mode = Mode; ///< CliMode enum value for this parser.

    /**
     * @brief Attempts to execute the first matching command in the pack.
     *
     * Convenience overload that accepts a pre-built token vector.
     *
     * @param ctx    Mutable execution context.
     * @param tokens Pre-split command tokens.
     * @return `true` if a command matched and executed successfully.
     */
    static bool execute(Context& ctx, const std::vector<std::string>& tokens)
    {
        return execute(ctx, tokens.begin(), tokens.end());
    }

    /**
     * @brief Attempts to execute the first matching command in the pack.
     *
     * Folds over `Commands...` and calls `tryExecute` (or `execute` for nested
     * parsers) until one succeeds, then stops.
     *
     * @tparam It Forward iterator over string-like elements.
     * @param ctx   Mutable execution context.
     * @param first Start of the token range.
     * @param last  End of the token range.
     * @return `true` if a command matched and executed successfully.
     */
    template <typename It>
    static bool execute(Context& ctx, It first, It last)
    {
        bool executed = false;

        auto tryOne = [&](auto cmdType)
        {
            using CmdT = decltype(cmdType);

            if (executed)
                return;

            if constexpr (is_cli_mode_v<CmdT>)
            {
                if (CmdT::execute(ctx, first, last))
                    executed = true;
            }
            else
            {
                if (CmdT::tryExecute(ctx, first, last))
                    executed = true;
            }
        };

        (tryOne(Commands{}), ...);

        return executed;
    }

    /**
     * @brief Convenience overload: tests whether a token vector matches any command in this mode.
     *
     * @param tokens Pre-split command tokens.
     * @return `true` if at least one command in the pack matches.
     */
    static bool match(const std::vector<std::string>& tokens)
    {
        return match(tokens.begin(), tokens.end());
    }

    /**
     * @brief Tests whether any command in the pack matches the given token range.
     *
     * Folds over `Commands...` and returns `true` as soon as one command's
     * `match()` method succeeds.  Does not execute any handler.
     *
     * @tparam It Forward iterator over string-like elements.
     * @param first Start of the token range.
     * @param last  End of the token range.
     * @return `true` if at least one command in the pack matches.
     */
    template <typename It>
    static bool match(It first, It last)
    {
        bool found = false;

        auto tryOne = [&](auto cmdType)
        {
            using CmdT = decltype(cmdType);

            if (found)
                return;

            if constexpr (is_cli_mode_v<CmdT>)
            {
                if (CmdT::match(first, last))
                    found = true;
            }
            else
            {
                if (CmdT::match(first, last))
                    found = true;
            }
        };

        (tryOne(Commands{}), ...);
        return found;
    }

    /**
     * @brief Annotates a JSON command-tree with support status for this mode.
     *
     * Walks the JSON structure rooted at `base` and calls `supportTraverse()`
     * on each command entry to mark it as FULL, PARTIAL, or unsupported. The
     * result is consumed by the web console to render command coverage.
     *
     * @param base  Root JSON object representing the full command tree. Modified in-place.
     */
    static void addSupport(nlohmann::json& base)
    {
        nlohmann::json* dir = &base;
        bool prompt = false;
        for (auto step : getPath(mode))
        {
            if (!prompt)
            {
                dir = &(*dir)[step];
                prompt = true;
            }
            else
                dir = &base[0][step];
        }

        std::pmr::unsynchronized_pool_resource pool;
        std::pmr::vector<std::pmr::string> tokList{&pool};
        tokList.reserve(256);

        for (auto& cmd : *dir)
        {
            supportTraverse(cmd, tokList, pool);
        }
    }
private:
    // SUPPORT ANNOTATION HELPERS

    /**
     * @brief Tri-state result of recursive command-tree support annotation.
     *
     * Returned by @ref supportTraverse to communicate the coverage level of
     * each JSON node back up the call stack.
     */
    enum class Support
    {
        FULL,    ///< Every sub-command under this node is implemented.
        PARTIAL, ///< At least one sub-command is implemented but not all.
        NONE     ///< No sub-commands under this node are implemented.
    };

    /**
     * @brief Recursively annotates a single JSON command node with support status.
     *
     * Pushes the node's `name` token onto `toks`, recurses into `subcommands`,
     * and sets the `"support"` JSON field according to the aggregate coverage.
     *
     * @param cmd   JSON object representing one command entry. Modified in-place.
     * @param toks  Accumulator of tokens from the root to the current node.
     * @param pool  Memory resource used for the token accumulator.
     * @return The support level for this node.
     */
    static Support supportTraverse(nlohmann::json& cmd, std::pmr::vector<std::pmr::string>& toks, std::pmr::unsynchronized_pool_resource& pool)
    {
        if (!cmd.is_object() || !cmd.contains("name") || !cmd["name"].is_string() ||
            !cmd.contains("subcommands") || !cmd["subcommands"].is_array())
            return Support::NONE;
        toks.emplace_back(cmd["name"].get<std::string>(), &pool);
        bool isMatch = match(toks.begin(), toks.end());
        size_t fullMatches = 0;
        bool partial = false;
        for (auto& sub : cmd["subcommands"])
        {
            switch (supportTraverse(sub, toks, pool))
            {
                case Support::FULL:
                    fullMatches++;
                    break;
                case Support::PARTIAL:
                    partial = true;
                    break;
                case Support::NONE:
                    break;
            }
        }
        toks.pop_back();
        
        if (partial)
        {
            cmd["support"] = nlohmann::json::boolean_t(true);
            return Support::PARTIAL;
        }
        if (isMatch && fullMatches == cmd["subcommands"].size())
        {
            cmd["support"] = nlohmann::json::boolean_t(false);
            return Support::FULL;
        }

        cmd.erase("support");
        return Support::NONE;
    }
};
}

#endif // CLI_MODE_PARSER_HPP
