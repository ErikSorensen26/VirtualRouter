/**
 * @file RouterEigrpNamedCommands.h
 * @brief CLI parser for EIGRP named mode (MD5-era) commands.
 *
 * Defines commands for EIGRP named mode configuration,
 * including address-family setup, timers, logging, and process-wide settings.
 */

#ifndef ROUTER_EIGRP_NAMED_COMMANDS_H
#define ROUTER_EIGRP_NAMED_COMMANDS_H

#include "configs/registry/router/EigrpRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli::execution
{
DEFINE_CMD_EXECUTOR(RouterEigrpNamed, CliMode::RouterEigrpNamed, config::EigrpNamedRegistry);
}

#endif // ROUTER_EIGRP_NAMED_COMMANDS_H
