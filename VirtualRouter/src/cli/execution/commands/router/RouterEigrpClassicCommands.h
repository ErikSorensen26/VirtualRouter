/**
 * @file RouterEigrpClassicCommands.h
 * @brief CLI parser for EIGRP classic mode commands.
 *
 * Defines commands for EIGRP classic (flat) configuration model,
 * including network/neighbor setup, address-family entry point,
 * metrics, logging, and topology access.
 */

#ifndef ROUTER_EIGRP_CLASSIC_COMMANDS_H
#define ROUTER_EIGRP_CLASSIC_COMMANDS_H

#include "configs/registry/router/EigrpRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli::execution
{
DEFINE_CMD_EXECUTOR(RouterEigrpClassic, CliMode::None, config::EigrpRegistry);
DEFINE_CMD_EXECUTOR(RouterEigrpClassicV4, CliMode::RouterEigrpClassicV4, config::EigrpRegistry);
DEFINE_CMD_EXECUTOR(RouterEigrpClassicVrf, CliMode::RouterEigrpClassicVRF, config::EigrpRegistry);
DEFINE_CMD_EXECUTOR(RouterEigrpClassicV6, CliMode::RouterEigrpClassicV6, config::EigrpRegistry);
}

#endif // ROUTER_EIGRP_COMMANDS_H
