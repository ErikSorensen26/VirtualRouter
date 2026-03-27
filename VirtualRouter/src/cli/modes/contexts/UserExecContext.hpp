/**
 * @file UserExecContext.hpp
 * @brief CLI execution context for User EXEC mode.
 */

#ifndef USER_EXEC_CONTEXT_HPP
#define USER_EXEC_CONTEXT_HPP

#include "ContextBase.hpp"

namespace cli
{
/**
 * @struct UserExecContext
 * @brief Execution context for User EXEC mode (`>` prompt).
 *
 * ## Architectural Role
 * User EXEC commands (e.g., `ping`, `traceroute`, `show version`) require no
 * additional subsystem references beyond those in `ContextBase`.  This struct
 * exists to satisfy the typed `Executor` / `CliModeParser` contract and to
 * keep the mode hierarchy explicit and extensible.
 *
 * ## Lifecycle & Ownership
 * - Created by `Executor::changeMode` on entry to `CliMode::UserExec`.
 * - Destroyed when the mode changes or the session ends.
 *
 * @see ContextBase
 * @ingroup CLI_MODE_CONTEXTS
 */
struct UserExecContext : ContextBase
{
    /**
     * @brief Constructs a User EXEC context from a base context.
     * @param base  Source base context (session reference and negate flag are copied).
     */
    UserExecContext(const ContextBase& base)
        : ContextBase(base) {}
};
}

#endif // USER_EXEC_CONTEXT_HPP
