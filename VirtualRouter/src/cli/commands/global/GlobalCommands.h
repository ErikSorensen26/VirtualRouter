/**
 * @file GlobalCommands.h
 * @brief Top-level CLI mode parser for the Global Configuration mode.
 *
 * Aggregates all commands available in `CliMode::GlobalConfiguration`, including
 * ARP management, hostname, routing protocol entry points (EIGRP, OSPF, BGP),
 * interface navigation, and sub-trees for `ip` and `ipv6`.
 */

#ifndef GLOBAL_COMMANDS_H
#define GLOBAL_COMMANDS_H

#include "configs/registry/global/GlobalRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(Global, CliMode::GlobalConfiguration, config::GlobalRegistry);
}

#endif // GLOBAL_COMMANDS_H
