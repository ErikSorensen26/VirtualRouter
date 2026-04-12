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

#include "cli/runtime/Token.hpp"
#include "cli/parser/CliModeParser.hpp"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

/// @brief Namespace enclosing all CLI subsystem types.
namespace cli
{

/**
 * @brief Compact descriptor for a previously-active CLI mode.
 *
 * Stored by `CliSession` in the navigation stack so that `popMode()` can
 * reconstruct the correct `Context<T>` without knowing `T` at pop-time.
 * The `constructFn` is a type-erased thunk captured when the mode was first
 * entered; it knows the concrete registry type and performs the placement-new.
 */
struct NavEntry
{
    CliMode  mode        = CliMode::None;                       ///< Which mode this frame represents.
    bool   (*executeFn)(ContextBase&, std::span<Token>) = nullptr; ///< Type-erased dispatch thunk.
    void   (*constructFn)(void*, void*, char*)          = nullptr; ///< Reconstructs Context<T> in-place.
    void*    configPtr   = nullptr;                             ///< Pointer to the registry instance.
};
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
    struct FindParserImpl
    {
        static_assert(dependent_false_v<std::integral_constant<CliMode, M>>,
                      "No CliModeParser found for the requested CliMode");
        using Type = void;
    };

    template <CliMode M, typename First, typename... Rest>
    struct FindParserImpl<M, First, Rest...>
        : std::conditional_t<(First::mode == M),
                             std::type_identity<First>,
                             FindParserImpl<M, Rest...>>
    {};

    /// @brief Resolves the concrete parser type for a given `CliMode` constant.
    /// @tparam M  The `CliMode` to look up; a static_assert fires if not found.
    template <CliMode M>
    using FindParser = typename FindParserImpl<M, Parsers...>::type;

public:

    // PUBLIC INTERFACE

    /**
     * @brief Constructs an Executor bound to a CLI session.
     * @param sess  The owning `CliSession`; must outlive this object.
     */
    explicit Executor(CliSession& sess)
        : session(sess)
    {}

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    Executor(Executor&&) = delete;
    Executor& operator=(Executor&&) = delete;

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
    template <CliMode M, typename C>
    void changeMode(C& config)
    {
        swap();

        using Parser = FindParser<M>;
        using Ctx = Context<C>;

        executeFn[head]   = executeThunk<Parser>;
        constructFn[head] = constructCtxThunk<C>;
        configPtr[head]   = static_cast<void*>(&config);

        void* buf = &storage[head];
        Ctx* ctx = new (buf) Ctx(session, config);

        ctxBuffer[head] = reinterpret_cast<ContextBase*>(ctx);
        currentMode[head] = M;
    }

    /**
     * @brief Returns whether the executor currently has an active mode.
     *
     * False only before the very first `changeMode` call (session init).
     * Used by `CliSession::changeMode` to decide whether to push a nav frame.
     */
    bool hasMode() const { return executeFn[head] != nullptr; }

    /**
     * @brief Captures the current mode into a `NavEntry` for nav-stack storage.
     *
     * The returned entry holds everything needed to later reconstruct the
     * current `Context<T>` via `restoreFromEntry`, without knowing `T`.
     */
    NavEntry captureCurrentMode() const
    {
        return { currentMode[head], executeFn[head], constructFn[head], configPtr[head] };
    }

    /**
     * @brief Reconstructs a previously-captured mode from a `NavEntry`.
     *
     * Swaps to the inactive ping-pong slot and placement-news the saved
     * `Context<T>` there, exactly mirroring what `changeMode` does on entry.
     *
     * @param entry  A frame previously returned by `captureCurrentMode`.
     */
    void restoreFromEntry(const NavEntry& entry)
    {
        swap();

        executeFn[head]   = entry.executeFn;
        constructFn[head] = entry.constructFn;
        configPtr[head]   = entry.configPtr;
        currentMode[head] = entry.mode;

        void* buf = &storage[head];
        entry.constructFn(static_cast<void*>(&session), entry.configPtr, static_cast<char*>(buf));

        ctxBuffer[head] = reinterpret_cast<ContextBase*>(buf);
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
     * @brief Resets the context by reverting the 'negate' and '
     * @warning The returned reference is invalidated by the next `changeMode` call.
     * @return Reference to the active `ContextBase` (concrete type varies by mode).
     */
    cli::ContextBase& getContext()
    {
        return *ctxBuffer[head];
    }

    /**
     * @brief Dispatches a flat token span to the active mode parser.
     *
     * Passes the token span directly to the active mode's `executeThunk`.
     * Segmentation is handled inside each `Command::tryExecute` so that
     * `SubCommand` can forward the raw span without re-segmenting.
     *
     * @param tokens  Flat token span from `CliSession::executeModeParser`.
     * @return True if the command was recognized and executed successfully.
     */
    bool execute(std::span<Token> tokens)
    {
        return executeFn[head](*ctxBuffer[head], tokens);
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
    /// @brief Signature of a type-erased parser dispatch function.
    using ExecuteFn = bool (*)(ContextBase&, std::span<Token>);
    /// @brief Signature of a type-erased context construction function.
    using ConstructFnPtr = void (*)(void*, void*, char*);

    /// @brief Toggles the active ping-pong slot index (0 ↔ 1).
    void swap() { head = (head == 0) ? 1 : 0; }

    template <typename Parser>
    static bool executeThunk(ContextBase& ctx, std::span<Token> tokens)
    {
        using Ctx = cli::Context<typename Parser::ContextType>;
        return Parser::execute(static_cast<Ctx&>(ctx), tokens);
    }

    /// @brief Type-erased construction thunk: placement-news `Context<C>` into `buf`.
    template <typename C>
    static void constructCtxThunk(void* sess, void* cfg, char* buf)
    {
        new (buf) Context<C>(
            *static_cast<CliSession*>(sess),
            *static_cast<C*>(cfg)
        );
    }

    size_t head = 0;                                    ///< Index of the currently active ping-pong slot (0 or 1).
    CliSession& session;                                ///< Back-reference to the owning session; non-owning.

    CliMode       currentMode[2]  = {};                 ///< Stored mode for each ping-pong slot.
    ExecuteFn     executeFn[2]    = {};                 ///< Dispatch function pointer for each ping-pong slot.
    ConstructFnPtr constructFn[2] = {};                 ///< Construction thunk for each ping-pong slot.
    void*          configPtr[2]   = {};                 ///< Registry pointer for each ping-pong slot.

    /// @brief Sentinel enum used only to compute the minimum `Context<>` size for the ping-pong storage slots.
    enum class Dummy { COUNT };
    struct DummyConfig { config::SubRegistry<Dummy> reg; }; ///< Dummy config struct to satisfy `Context`'s registry requirement.
    /// @brief Raw aligned storage for two `Context<>` instances; avoids heap allocation on mode switch.
    using Storage = std::aligned_storage_t<sizeof(Context<DummyConfig>), alignof(Context<DummyConfig>)>;
    Storage storage[2] = {};
    /// @brief Pointers into `storage`; null until the first `changeMode` call for that slot.
    ContextBase* ctxBuffer[2] { nullptr, nullptr };
};
}

#endif // EXECUTOR_HPP
