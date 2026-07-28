/**
 * @file ExecutionContext.hpp
 * @brief Instantiates the concrete `ExecutionManager` type alias for the router CLI.
 *
 * Aggregates every registered `CliModeParser` specialization into a single
 * `cli::execution::Executor` template instantiation.  All mode parsers must be listed
 * here; adding a new mode requires only appending its parser type to the
 * `ExecutionManager` alias.
 */

/**
 * @defgroup CLI_EXECUTION CLI Execution
 * @ingroup CLI
 * @brief Executor template instantiation and ExecutionContext binding.
 */

#ifndef EXECUTION_CONTEXT_HPP
#define EXECUTION_CONTEXT_HPP

#include "configs/RegistryReference.hpp"
#include "cli/execution/Executor.hpp"

#include "cli/execution/commands/global/GlobalCommands.h"
#include "cli/execution/commands/interface/InterfaceCommands.h"
#include "cli/execution/commands/PrivilegedExecCommands.h"
#include "cli/execution/commands/UserExecCommands.h"


#include "cli/execution/commands/router/RouterEigrpClassicCommands.h"
#include "cli/execution/commands/router/RouterEigrpNamedCommands.h"
#include "cli/execution/commands/router/RouterEigrpAddressFamilyCommands.h"
#include "cli/execution/commands/router/RouterEigrpInterfaceCommands.h"
#include "cli/execution/commands/router/RouterEigrpTopologyCommands.h"

namespace cli::execution
{
/**
 * @brief Concrete `Executor` instantiation that binds all registered CLI mode parsers.
 *
 * `ExecutionManager` is the single object created per `CliSession` that owns
 * the active context and routes each command to the correct parser.  Adding
 * support for a new `CliMode` requires:
 * 1. Defining a `CliModeParser<NewMode>` specialization.
 * 2. Adding `NewModeCommands` to the variadic list below.
 *
 * @note The `Executor` static assertions guarantee no duplicate `CliMode`
 *       values and that every entry satisfies `is_cli_mode_v`.
 *
 * @ingroup CLI
 */
using ExecutionManager = cli::execution::Executor<
    UserExecCommands,
    PrivilegedExecCommands,
    GlobalCommands,
    InterfaceCommands,
    RouterEigrpClassicV4Commands,
    RouterEigrpClassicV6Commands,
    RouterEigrpClassicVrfCommands,
    /* RouterEigrpClassicV6Commands */
    RouterEigrpNamedCommands,
    RouterEigrpAddressFamilyV4Commands,
    RouterEigrpAddressFamilyV6Commands,
    RouterEigrpInterfaceV4Commands,
    RouterEigrpInterfaceV6Commands,
    RouterEigrpTopologyV4Commands,
    RouterEigrpTopologyV6Commands
>;
}

#endif // EXECUTION_CONTEXT_HPP
