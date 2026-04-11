/**
 * @file GlobalIPCommands.h
 * @brief CLI parser for the `ip` sub-tree of Global Configuration mode.
 *
 * Defines commands reachable via `ip <...>` in `CliMode::GlobalConfiguration`,
 * currently limited to the DHCP sub-tree delegated to `GlobalIPDHCPCommands`.
 */

#ifndef GLOBAL_IP_COMMANDS_H
#define GLOBAL_IP_COMMANDS_H

#include "configs/registry/global/GlobalRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(GlobalIP, CliMode::GlobalConfiguration, config::GlobalRegistry);
}

#endif
