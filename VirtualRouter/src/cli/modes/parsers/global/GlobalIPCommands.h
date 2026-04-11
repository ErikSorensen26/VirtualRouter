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
#include "configs/registry/global/GlobalRegistry.h"

#define GLOBAL_PARAMS DEFINE_PARAMS(config::GlobalRegistry)
#define GLOBAL_SUB_PARAMS DEFINE_SUB_PARAMS(config::GlobalRegistry)

namespace cli
{
bool GlobalIP_DHCP_SubHandler(GLOBAL_SUB_PARAMS);

#define GLOBAL_IP_LIST(X, Y) \
    X(Y, (_SUB_, DHCP, "dhcp"_tok))

DEFINE_CMD_MODE(GlobalIP, CliMode::GlobalConfiguration, config::GlobalRegistry, GLOBAL_IP_LIST);
}

#undef GLOBAL_IP_LIST
#undef GLOBAL_PARAMS
#undef GLOBAL_SUB_PARAMS

#endif
