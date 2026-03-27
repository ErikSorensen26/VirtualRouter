/**
 * @file ContextBase.hpp
 * @brief Base type for all CLI mode-specific execution contexts.
 *
 * Every `CliModeParser` specialization defines a `ContextType` that derives
 * from `ContextBase`.  The base carries the minimum state shared by all modes:
 * a reference to the owning `CliSession` and the negation flag set when the
 * user prefixes a command with `no`.
 */

/**
 * @defgroup CLI_MODE_CONTEXTS CLI Mode Contexts
 * @ingroup CLI_MODES
 * @brief Per-mode execution context types for User Exec, Privileged, Global, Interface, and protocol modes.
 */

#ifndef CONTEXT_BASE_HPP
#define CONTEXT_BASE_HPP

/// @brief Namespace enclosing all CLI subsystem types.
namespace cli
{
class CliSession;

/**
 * @struct ContextBase
 * @brief Polymorphic base for all CLI mode execution contexts.
 *
 * ## Architectural Role
 * `ContextBase` is the type-erased handle stored in `Executor`'s ping-pong
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
struct ContextBase
{
    /**
     * @brief Constructs a root context bound directly to a CLI session.
     * @param term  The owning session; must outlive this context.
     */
    ContextBase(CliSession& term) : terminal(term) {}

    /**
     * @brief Copy-constructs a context, propagating session reference and negate flag.
     *
     * Used by `Executor::changeMode` when a previous context exists, so that
     * the negate flag set during command tokenization is carried into the new
     * mode's context.
     *
     * @param base  Source context to copy from.
     */
    ContextBase(const ContextBase& base) : terminal(base.terminal), negate(base.negate) {}

    /**
     * @brief Virtual destructor; enables correct destruction through base pointer.
     */
    virtual ~ContextBase() = default;

    CliSession& terminal;   ///< Reference to the owning CLI session.
    bool negate = false;    ///< True when the command is a `no`-form negation.
};
}

#endif // CONTEXT_BASE_HPP
