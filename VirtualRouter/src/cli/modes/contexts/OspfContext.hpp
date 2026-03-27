/**
 * @file OspfContext.hpp
 * @brief CLI execution context for OSPF router configuration modes.
 */

#ifndef OSPF_CONTEXT_HPP
#define OSPF_CONTEXT_HPP

#include "ContextBase.hpp"

/// @brief Convenience macro for OSPF command handler parameter lists.
#define OSPF_PARAMS OspfContext& ctx, const std::vector<std::string>& args

namespace routing::ospf { class OspfProcess; }

namespace cli
{
/**
 * @struct OspfContext
 * @brief Execution context carrying an OSPF process reference for router OSPF mode commands.
 *
 * ## Architectural Role
 * Grants command handlers access to the specific `OspfProcess` instance that
 * the user entered (e.g., `router ospf 1`).  All OSPF router-mode commands
 * read and write OSPF configuration through this reference.
 *
 * ## Lifecycle & Ownership
 * - Created by `Executor::changeMode` on entry to any OSPF router configuration mode.
 * - `ospf` is a non-owning reference; the `OspfProcess` is owned by the routing
 *   subsystem and must outlive this context.
 *
 * @see ContextBase
 * @ingroup CLI_MODE_CONTEXTS
 */
struct OspfContext : ContextBase
{
    /**
     * @brief Constructs an OSPF router configuration context.
     * @param base     Source base context (session reference and negate flag are copied).
     * @param process  Reference to the OSPF process being configured.
     */
    OspfContext(const ContextBase& base, routing::ospf::OspfProcess& process)
        : ContextBase(base), ospf(process) {}

    routing::ospf::OspfProcess& ospf; ///< The OSPF process being configured; non-owning.
};
}

#endif // GLOBAL_CONTEXT_HPP
