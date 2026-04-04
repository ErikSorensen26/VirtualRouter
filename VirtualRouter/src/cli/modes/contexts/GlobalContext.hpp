/**
 * @file GlobalContext.hpp
 * @brief CLI execution context for Global Configuration mode.
 */

#ifndef GLOBAL_CONTEXT_HPP
#define GLOBAL_CONTEXT_HPP

#include "ContextBase.hpp"
#include "cli/runtime/Token.hpp"

namespace core { class Global; }
namespace core { class VirtualRouter; }

/// @brief Convenience macro for Global Configuration command handler parameter lists.
#define GLOBAL_PARAMS GlobalContext& ctx, const std::vector<std::span<cli::Token>>& segs

namespace cli
{
/**
 * @struct GlobalContext
 * @brief Execution context carrying global and VRF references for Global Configuration mode commands.
 *
 * ## Architectural Role
 * Grants command handlers access to the system-wide `Global` object (for
 * cross-VRF operations) and to the currently selected `VirtualRouter` instance
 * (VRF) that configuration changes should target.
 *
 * ## Lifecycle & Ownership
 * - Created by `Executor::changeMode` on entry to `CliMode::GlobalConfiguration`.
 * - All members are non-owning references; the referenced objects are owned by
 *   the core routing subsystem and must outlive this context.
 *
 * @see ContextBase
 * @ingroup CLI_MODE_CONTEXTS
 */
struct GlobalContext : ContextBase
{
    /**
     * @brief Constructs a Global Configuration context.
     * @param base  Source base context (session reference and negate flag are copied).
     * @param glob  Reference to the system-wide global instance.
     * @param vrf   Reference to the currently active VRF / routing-table instance.
     */
    GlobalContext(const ContextBase& base, core::Global& glob, core::VirtualRouter& vrf)
        : ContextBase(base), global(glob), vrf(vrf) {}

    core::Global& global;       ///< System-wide routing and subsystem registry; non-owning.
    core::VirtualRouter& vrf;   ///< Currently targeted VRF instance; non-owning.
};
}

#endif // GLOBAL_CONTEXT_HPP
