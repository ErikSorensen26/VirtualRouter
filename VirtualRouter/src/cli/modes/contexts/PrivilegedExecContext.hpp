/**
 * @file PrivilegedExecContext.hpp
 * @brief CLI execution context for Privileged EXEC mode.
 */

#ifndef PRIVILEGED_EXEC_CONTEXT_HPP
#define PRIVILEGED_EXEC_CONTEXT_HPP

#include "ContextBase.hpp"

namespace cli
{
/**
 * @struct PrivilegedExecContext
 * @brief Execution context for Privileged EXEC mode (`#` prompt).
 *
 * ## Architectural Role
 * Privileged EXEC commands (e.g., `show`, `debug`, `configure terminal`)
 * require no additional subsystem references beyond those in `ContextBase`;
 * this struct exists to satisfy the typed `Executor` / `CliModeParser`
 * contract while keeping the mode hierarchy explicit.
 *
 * ## Lifecycle & Ownership
 * - Created by `Executor::changeMode` on entry to `CliMode::PrivilegedExec`.
 * - Destroyed when the mode changes or the session ends.
 *
 * @see ContextBase
 * @ingroup CLI_MODE_CONTEXTS
 */
struct PrivilegedExecContext : ContextBase
{
    /**
     * @brief Constructs a Privileged EXEC context from a base context.
     * @param base  Source base context (session reference and negate flag are copied).
     */
    PrivilegedExecContext(const ContextBase& base)
        : ContextBase(base) {}
};
}

#endif // PRIVILEGED_EXEC_CONTEXT_HPP
