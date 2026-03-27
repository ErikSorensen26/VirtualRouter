/**
 * @file GlobalIPCommands.h
 * @brief CLI parser for the `ip` sub-tree of Global Configuration mode.
 *
 * Defines commands reachable via `ip <...>` in `CliMode::GlobalConfiguration`,
 * currently limited to the DHCP sub-tree delegated to `GlobalIPDHCPCommands`.
 */

#ifndef GLOBAL_IP_COMMANDS_H
#define GLOBAL_IP_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/SubCommand.hpp"
#include "cli/modes/contexts/GlobalContext.hpp"
#include "GlobalIPDHCPCommands.h"

namespace cli
{
bool GlobalIP_DHCP_Handler(GLOBAL_PARAMS);
using GlobalIP_DHCP = subAdder<GlobalContext,
    GlobalIPDHCPCommands,
    "dhcp"_tok
>;

/**
 * @brief Parser for the `ip` sub-tree in Global Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::GlobalConfiguration` with `GlobalContext` and
 * currently exposes the `ip dhcp` sub-tree.
 */
using GlobalIPCommands = CliModeParser<CliMode::GlobalConfiguration, GlobalContext,
    GlobalIP_DHCP
>;
}

#endif
