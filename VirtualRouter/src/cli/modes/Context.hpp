/**
 * @file Context.hpp
 * @brief Base type for all CLI mode-specific execution contexts.
 *
 * Every `CliModeParser` specialization defines a `ContextType` that derives
 * from `Context`.  The base carries the minimum state shared by all modes:
 * a reference to the owning `CliSession` and the negation flag set when the
 * user prefixes a command with `no`.
 */

/**
 * @defgroup CLI_MODE_CONTEXTS CLI Mode Contexts
 * @ingroup CLI_MODES
 * @brief Per-mode execution context types for User Exec, Privileged, Global, Interface, and protocol modes.
 */

#ifndef CONTEXT_HPP
#define CONTEXT_HPP

#include "configs/SubRegistry.hpp"
namespace cli { struct CliModeParserFlag {}; }

/// @brief Entry type tag: plain @ref Command descriptor.
#define COMMAND 0
/// @brief Entry type tag: @ref SubCommand prefix dispatcher.
#define SUBPRSR 1
/// @brief Entry type tag: mode extension (external command list injection).
#define CMD_INHERIT 2

/// @brief Silences unused-parameter warnings for command handler parameters.
#define UNUSED(x) (void)(x)

/// @brief Appends a `commandAdder<name##_Handler, __VA_ARGS__>` to a parser list.
#define DEFINE_CMD(name, context, ...) \
    , commandAdder<name##_Handler __VA_OPT__(, __VA_ARGS__)>

/// @brief Appends a `subAdder<submode, __VA_ARGS__>` to a parser list.
#define DEFINE_SUB(name, context, ...) \
    , subAdder<name##_SubHandler __VA_OPT__(, __VA_ARGS__)>

// Select which macro to call based on flag
#define SELECT_CMD_0(prefix, name, ctx, ...) \
    DEFINE_CMD(prefix##_##name, ctx __VA_OPT__(, __VA_ARGS__))
#define SELECT_CMD_1(prefix, name, ctx, ...) \
    DEFINE_SUB(prefix##_##name, ctx __VA_OPT__(, __VA_ARGS__))
#define SELECT_CMD_2(prefix, name, ...) \
    , name
    
// Expand a single command line (flag, name, token) with injected prefix or ctx_or_sub
#define EXPAND_COMMAND_WRAPPER(ctx, cmd) \
    EXPAND_COMMAND_WRAPPER_IMPL(EXPAND_ARGS ctx, EXPAND_ARGS cmd)
#define EXPAND_COMMAND_WRAPPER_IMPL(prefix, context, ...) \
    EXPAND_COMMAND(prefix, context __VA_OPT__(, __VA_ARGS__))
#define EXPAND_COMMAND(prefix, context, flag, name, ...) \
    SELECT_CMD_##flag(prefix, name, context __VA_OPT__(, __VA_ARGS__))

// Argument Expander
#define EXPAND_ARGS(...) __VA_ARGS__

#define DEFINE_MODE(prefix, mode, context, ...) \
    using prefix##Commands = cli::execution::CliModeParser<mode, context __VA_ARGS__>;

// Define a full command grep
#define DEFINE_CMD_MODE(prefix, context, list) \
    using prefix##Executor = CliModeParser<context \
    list(EXPAND_COMMAND_WRAPPER, (prefix, context))>; \
    bool prefix##Commands::execute(Context<context>& ctx, std::span<Token> toks, size_t idx) \
    { return prefix##Executor::execute(ctx, toks, idx); }

// Define the execution function
#define DEFINE_CMD_EXECUTOR(prefix, climode, context) \
    struct prefix##Commands : CliModeParserFlag \
    { \
        using ContextType = context; \
        static constexpr CliMode mode = climode; \
        static bool execute(Context<context>& ctx, std::span<Token> toks, size_t idx = 0); \
    }

// Define parameter list
#define DEFINE_PARAMS(config) \
    cli::Context<config>& ctx, const std::vector<std::span<Token>>& segs

// Define sub parameter list
#define DEFINE_SUB_PARAMS(config) \
    cli::Context<config>& ctx, std::span<Token> toks, size_t idx

/// @brief Namespace enclosing all CLI subsystem types.
namespace cli
{
struct Token;
class CliSession;

/**
 * @brief Non-template base for all CLI mode execution contexts.
 * @ingroup CLI_MODE_CONTEXTS
 *
 * Carries the minimum state every mode needs: a reference to the owning
 * `CliSession` and the flags that control whether the current command is a
 * `no`-form negation or a `default`-form reset.
 *
 * ## Architectural Role
 * Stored in `Executor`'s ping-pong buffer as a type-erased `ContextBase*`
 * so that the `Executor` can operate without knowing the concrete context
 * type.  Command dispatch recovers the concrete type via the
 * `executeThunk` stored alongside it.
 *
 * ## Lifecycle & Ownership
 * Owned exclusively by the `Executor` that created it; destroyed on the
 * next `changeMode` or when the session ends.
 *
 * @see Executor
 * @see Context
 */
class ContextBase
{
public:
    ContextBase(CliSession& term)
        : terminal(term)
    {}
    ContextBase(CliSession& term, void* cfg)
        : terminal(term), ctx(cfg)
    {}

    CliSession& terminal;    ///< Reference to the owning CLI session.
    void* ctx = nullptr; ///< Opaque pointer to the mode-specific config registry (for use in executeThunks).

    /**
     * @brief Which registry @ref ctx points at, as a `config::registryIdV`.
     *
     * The pointer is erased on the way in and recovered by casting it back to
     * whatever registry a command's configId names. Nothing in that round trip
     * checks the two agree, so a node bound to the wrong registry -- a grammar
     * typo, a stale Commands.bin -- reads a field at an offset that means
     * nothing in the object actually there.
     *
     * Kept beside the pointer rather than derived, because a void* is exactly
     * the thing that cannot say what it points to. `NO_REGISTRY` means untagged:
     * checks pass, for contexts built before this was threaded through.
     */
    uint16_t ctxRegistry = NO_REGISTRY;

    /// @brief An untagged context, which the registry check waves through.
    static constexpr uint16_t NO_REGISTRY = 0xFFFF;

    /**
     * @brief Points the context at a registry and records which one it is.
     *
     * The two always move together -- a pointer set without its tag is exactly
     * the untagged state the tag exists to remove -- so they are set together
     * rather than left to each call site to remember.
     */
    void rescope(void* cfg, uint16_t registry) noexcept
    {
        ctx = cfg;
        ctxRegistry = registry;
    }

    bool negate = false;     ///< True when the command was entered with a `no` prefix.
    bool defaulted = false;  ///< True when the command was entered with a `default` prefix.
};


/**
 * @struct Context
 * @brief Polymorphic base for all CLI mode execution contexts.
 *
 * ## Architectural Role
 * `Context` is the type-erased handle stored in `Executor`'s ping-pong
 * buffer.  Each concrete context subclass extends it with references to the
 * subsystem objects that the commands in that mode need (e.g., `Interface&`,
 * `EigrpProcess*`).
 *
 * ## Lifecycle & Ownership
 * - Created by `Executor::changeMode` using `std::make_unique`.
 * - Owned exclusively by the `Executor` that created it.
 * - Destroyed automatically when the mode slot is overwritten or the session ends.
 *
 * ## Concurrency Model
 * Not thread-safe; all access must occur on the session's thread.
 *
 * @see Executor
 * @ingroup CLI
 */
template <typename T>
requires config::IsSubRegistryWrapper<T>
class Context : public ContextBase
{
public:
    /**
     * @brief Constructs a root context bound directly to a CLI session.
     * @param term  The owning session; must outlive this context.
     */
    Context(CliSession& term, T& c) : ContextBase(term, &c) {}

    /**
     * @brief Accessor for the mode-specific config registry.
     * @return Reference to the mode's configuration registry.
     */
    T& configs() { return *static_cast<T*>(ctx); }
};
}

#endif // CONTEXT_BASE_HPP
