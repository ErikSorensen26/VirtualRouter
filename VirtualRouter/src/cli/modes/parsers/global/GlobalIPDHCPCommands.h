/**
 * @file GlobalIPDHCPCommands.h
 * @brief CLI parser for the `ip dhcp` sub-tree of Global Configuration mode.
 *
 * Defines commands reachable via `ip dhcp <...>` in `CliMode::GlobalConfiguration`.
 * Covers DHCP binding management, BOOTP support, conflict handling, database
 * configuration, debug flags, and excluded-address ranges.
 */

#ifndef GLOBAL_IP_DHCP_COMMANDS_H
#define GLOBAL_IP_DHCP_COMMANDS_H

#include "configs/registry/global/GlobalRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(GlobalIPDHCP, CliMode::GlobalConfiguration, config::GlobalRegistry);
}

#endif // GLOBAL_IP_DHCP_COMMANDS_H
