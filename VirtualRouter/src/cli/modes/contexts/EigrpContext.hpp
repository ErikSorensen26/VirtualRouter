/**
 * @file EigrpContext.hpp
 * @brief CLI execution context for all EIGRP router configuration modes.
 */

#ifndef EIGRP_CONTEXT_HPP
#define EIGRP_CONTEXT_HPP

#include "ContextBase.hpp"
#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include "cli/runtime/Token.hpp"

/// @brief Convenience macro for EIGRP command handler parameter lists.
#define EIGRP_PARAMS EigrpContext& ctx, const std::vector<std::span<cli::Token>>& segs

namespace routing::eigrp { class Eigrp; class EigrpNamed; }

namespace cli
{
/**
 * @struct EigrpContext
 * @brief Execution context carrying all EIGRP subsystem references needed by command handlers.
 *
 * ## Architectural Role
 * Passed by reference to every EIGRP command handler (classic, named, address-family,
 * interface, and topology sub-modes).  Handlers read and write the pointed-to EIGRP
 * objects to apply configuration.
 *
 * ## Lifecycle & Ownership
 * - Created by `Executor::changeMode` when entering any EIGRP configuration mode.
 * - All pointer members are non-owning; the objects they point to are owned by the
 *   routing subsystem and must outlive the context.
 * - `tempEigrp` may be null; handlers must null-check before use.
 *
 * @see ContextBase
 * @ingroup CLI_MODE_CONTEXTS
 */
struct EigrpContext : ContextBase
{
    /**
     * @brief Constructs an EIGRP context from a base context and EIGRP subsystem pointers.
     *
     * @param base   Source base context (session reference and negate flag are copied).
     * @param eigrp  Active classic EIGRP process; may be null if in named mode only.
     * @param named  Active named EIGRP process; may be null if in classic mode only.
     * @param iface  Active EIGRP interface configuration registry; may be null.
     * @param temp   Temporary EIGRP process handle used during transient operations; defaults to null.
     */
    EigrpContext(const ContextBase& base, routing::eigrp::Eigrp* eigrp, routing::eigrp::EigrpNamed* named, config::EigrpInterfaceRegistry* iface, routing::eigrp::Eigrp* temp = nullptr)
        : ContextBase(base), currentEigrp(eigrp), currentEigrpNamed(named), currentEigrpInterface(iface), tempEigrp(temp) {}

    routing::eigrp::Eigrp* currentEigrp;                    ///< Active classic EIGRP process; non-owning, may be null.
    routing::eigrp::EigrpNamed* currentEigrpNamed;          ///< Active named EIGRP process; non-owning, may be null.
    config::EigrpInterfaceRegistry* currentEigrpInterface;  ///< Active EIGRP interface config registry; non-owning, may be null.
    routing::eigrp::Eigrp* tempEigrp;                       ///< Temporary EIGRP handle for transient operations; non-owning, may be null.
};
}

#endif // GLOBAL_CONTEXT_HPP
