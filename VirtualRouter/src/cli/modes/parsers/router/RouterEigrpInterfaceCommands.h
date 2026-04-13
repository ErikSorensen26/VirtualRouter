/**
 * @file RouterEigrpInterfaceCommands.h
 * @brief CLI parser for EIGRP interface-level commands.
 *
 * Defines interface-specific EIGRP commands including delay, bandwidth,
reliability, hello interval, hold time, and split horizon settings.
 */

#ifndef ROUTER_EIGRP_INTERFACE_COMMANDS_H
#define ROUTER_EIGRP_INTERFACE_COMMANDS_H

#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(RouterEigrpInterface, CliMode::None, config::EigrpInterfaceRegistry);
DEFINE_CMD_EXECUTOR(RouterEigrpInterfaceV4, CliMode::RouterEigrpInterfaceV4, config::EigrpInterfaceRegistry);
DEFINE_CMD_EXECUTOR(RouterEigrpInterfaceV6, CliMode::RouterEigrpInterfaceV6, config::EigrpInterfaceRegistry);
}

#endif // ROUTER_EIGRP_INTERFACE_COMMANDS_H
