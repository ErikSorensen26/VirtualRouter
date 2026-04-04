/**
 * @file InterfaceContext.hpp
 * @brief CLI execution context for Interface Configuration mode.
 */

#ifndef INTERFACE_CONTEXT_HPP
#define INTERFACE_CONTEXT_HPP

#include "ContextBase.hpp"
#include "cli/runtime/Token.hpp"

namespace interface { class Interface; }

/// @brief Convenience macro for Interface Configuration command handler parameter lists.
#define INTERFACE_PARAMS InterfaceContext& ctx, const std::vector<std::span<cli::Token>>& segs

namespace cli
{
/**
 * @struct InterfaceContext
 * @brief Execution context carrying an interface reference for Interface Configuration mode commands.
 *
 * ## Architectural Role
 * Grants command handlers a reference to the specific `Interface` object that
 * the user selected (e.g., `interface ethernet 0/0`).  All interface-mode
 * commands read and write through this reference.
 *
 * ## Lifecycle & Ownership
 * - Created by `Executor::changeMode` on entry to `CliMode::Interface`.
 * - `currentInterface` is a non-owning reference; the `Interface` object is
 *   owned by `InterfaceManager` and must outlive this context.
 *
 * @see ContextBase
 * @ingroup CLI_MODE_CONTEXTS
 */
struct InterfaceContext : ContextBase
{
    /**
     * @brief Constructs an Interface Configuration context.
     * @param base   Source base context (session reference and negate flag are copied).
     * @param iface  Reference to the interface being configured.
     */
    InterfaceContext(const ContextBase& base, interface::Interface& iface)
        : ContextBase(base), currentInterface(iface) {}

    interface::Interface& currentInterface; ///< The interface being configured; non-owning.
};
}

#endif // INTERFACE_CONTEXT_HPP
