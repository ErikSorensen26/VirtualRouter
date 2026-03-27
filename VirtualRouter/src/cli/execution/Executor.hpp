/**
 * @file Executor.hpp
 * @brief Variadic, compile-time CLI mode dispatcher.
 *
 * Defines `cli::Executor<Parsers...>`, the central command-dispatch engine
 * that owns the active `ContextBase` and routes each token stream to the
 * correct `CLI_MODE_PARSER`.  Mode transitions are O(1) and allocation-safe
 * thanks to a two-slot context ping-pong buffer.
 */

#ifndef EXECUTOR_HPP
#define EXECUTOR_HPP

#include "cli/parser/CliModeParser.hpp"
#include "cli/modes/contexts/ContextBase.hpp"

/// @brief Namespace enclosing all CLI subsystem types.
namespace cli
{
/**
 * @class Executor
 * @brief Variadic, compile-time CLI mode dispatcher.
 *
 * `Executor` holds up to two live `ContextBase` slots in a ping-pong array so
 * that `changeMode` and `revert` are O(1) without heap re-allocation on every
 * mode switch.  The active slot is selected by the `head` index.
 *
 * ## Architectural Role
 * - Owns the sole `ContextBase` allocation for the active (and optionally the
 *   previous) CLI mode.
 * - Resolves, at compile time, the parser type for any `CliMode` constant via
 *   the `FindParser` meta-function.
 * - Provides the single `execute()` entry point used by `CliSession` to
 *   dispatch each tokenized command line.
 *
 * ## Lifecycle & Ownership
 * - Created once per `CliSession` and lives for the session's lifetime.
 * - Owns all `ContextBase` instances it creates; they are destroyed on the
 *   next `changeMode` or when the `Executor` is destroyed.
 *
 * ## Concurrency Model
 * Not thread-safe; all calls must originate from the owning session thread.
 *
 * @tparam Parsers  One or more `CliModeParser` specializations. Each must
 *                  satisfy `cli::is_cli_mode_v<P>` and carry a unique
 *                  `P::mode` constant.
 *
 * @ingroup CLI_EXECUTION
 */
template <typename... Parsers>
class Executor
{
    static_assert(
        (cli::is_cli_mode_v<Parsers> && ...),
        "All entries must be CliModeParser types"
    );

    // COMPILE-TIME VALIDATION

    /// @brief Helper predicate: asserts that no two parsers share the same CliMode value.
    template <CliMode...>
    struct UniqueModes : std::true_type{};

    template <CliMode M, CliMode... Rest>
    struct UniqueModes<M, Rest...>
        : std::bool_constant<
            ((M != Rest) && ...) &&
            UniqueModes<Rest...>::value
        > {};

    static_assert(
        UniqueModes<Parsers::mode...>::value,
        "Duplicate CliMode detected"
    );

    // PARSER TYPE RESOLUTION

    /// @brief Always-false sentinel used to produce a meaningful static_assert message.
    template <typename>
    static constexpr bool dependent_false_v = false;

    template <CliMode M, typename... Ts>
    struct FindParserImpl;

    template <CliMode M, typename First, typename... Rest>
        requires(First::mode == M)
    struct FindParserImpl<M, First, Rest...>
    {
        using Type = First;
    };

    template <CliMode M, typename First, typename... Rest>
        requires (First::mode != M)
    struct FindParserImpl<M, First, Rest...> : FindParserImpl<M, Rest...> {};

    template <CliMode M>
    struct FindParserImpl<M>
    {
        static_assert(dependent_false_v<std::integral_constant<CliMode, M>>,
                      "No CliModeParser found for the requested CliMode");
        using Type = void;
    };

    /// @brief Resolves the concrete parser type for a given `CliMode` constant.
    /// @tparam M  The `CliMode` to look up; a static_assert fires if not found.
    template <CliMode M>
    using FindParser = typename FindParserImpl<M, Parsers...>::Type;

public:

    // PUBLIC INTERFACE

    /**
     * @brief Constructs an Executor bound to a CLI session.
     * @param sess  The owning `CliSession`; must outlive this object.
     */
    explicit Executor(CliSession& sess)
        : session(sess)
    {}

    /**
     * @brief Type-erased thunk that down-casts the context and calls the parser.
     *
     * Stored as a function pointer in `executeFn` so that the active parser
     * can be called without virtual dispatch.
     *
     * @tparam Parser  The concrete `CliModeParser` whose `execute` to call.
     * @param ctx  Base-class context reference; down-cast to `Parser::ContextType`.
     * @param b    Begin iterator of the token range.
     * @param e    End iterator of the token range.
     * @return True if the command was recognized and executed.
     */
    template <typename Parser>
    static bool executeThunk(
        cli::ContextBase& ctx,
        std::vector<std::string>::const_iterator b,
        std::vector<std::string>::const_iterator e)
    {
        using Ctx = typename Parser::ContextType;
        static_assert(std::is_base_of_v<cli::ContextBase, Ctx>,
                      "Parser::ContextType must derive from cli::ContextBase");
        return Parser::execute(static_cast<Ctx&>(ctx), b, e);
    }

    /**
     * @brief Transitions to a new CLI mode, constructing the appropriate context.
     *
     * Swaps to the inactive slot, constructs a new `Parser::ContextType` (copying
     * base fields from the previous context when one exists), and stores a pointer
     * to the matching `executeThunk`.
     *
     * @tparam M     Target `CliMode`.
     * @tparam Args  Extra arguments forwarded to the context constructor after
     *               the `ContextBase` copy source or `CliSession` reference.
     * @param args   Subsystem references required by the target context type.
     */
    template <CliMode M, typename... Args>
    void changeMode(Args&&... args)
    {
        swap();

        using Parser = FindParser<M>;
        using Ctx = typename Parser::ContextType;

        executeFn[head] = executeThunk<Parser>;

        if (modeConfig[head ^ 1])
        {
            modeConfig[head] =
                std::make_unique<Ctx>(*modeConfig[head ^ 1], std::forward<Args>(args)...);
        }
        else
        {
            modeConfig[head] =
                std::make_unique<Ctx>(session, std::forward<Args>(args)...);
        }

        currentMode[head] = M;
    }

    /**
     * @brief Returns the currently active `CliMode`.
     * @return The mode stored in the active slot.
     */
    CliMode getMode()
    {
        return currentMode[head];
    }

    /**
     * @brief Returns a reference to the currently active context.
     * @warning The returned reference is invalidated by the next `changeMode` call.
     * @return Reference to the active `ContextBase` (concrete type varies by mode).
     */
    cli::ContextBase& getContext()
    {
        return *modeConfig[head];
    }

    /**
     * @brief Dispatches a tokenized command to the active mode parser.
     * @param tokens  Whitespace-split command tokens from the session input.
     * @return True if the command was recognized and executed successfully.
     */
    bool execute(const std::vector<std::string>& tokens)
    {
        return executeFn[head](
            *modeConfig[head],
            tokens.begin(),
            tokens.end()
        );
    }

    /**
     * @brief Reverts to the previously active mode by swapping the ping-pong slots.
     *
     * Used to implement `exit` and `end` semantics without re-constructing the
     * prior context.
     */
    void revert()
    {
        swap();
    }

private:

    // PRIVATE TYPES

    /// @brief Signature of a type-erased parser dispatch function.
    using ExecuteFn = bool (*)(
        cli::ContextBase&,
        std::vector<std::string>::const_iterator,
        std::vector<std::string>::const_iterator
    );

    // PRIVATE HELPERS

    /// @brief Toggles the active ping-pong slot index (0 ↔ 1).
    void swap() { head = (head == 0) ? 1 : 0; }

    // PRIVATE MEMBERS

    size_t head = 0;                                    ///< Index of the currently active ping-pong slot (0 or 1).
    CliSession& session;                                ///< Back-reference to the owning session; non-owning.
    CliMode currentMode[2];                             ///< Stored mode for each ping-pong slot.
    std::unique_ptr<cli::ContextBase> modeConfig[2];    ///< Owned context for each ping-pong slot.
    ExecuteFn executeFn[2] = {};                        ///< Dispatch function pointer for each ping-pong slot.
};
}

#endif // EXECUTOR_HPP
