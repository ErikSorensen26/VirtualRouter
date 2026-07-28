/**
 * @file InterfaceCommands.h
 * @brief Top-level CLI mode parser for the Interface Configuration mode.
 *
 * Aggregates all commands available in `CliMode::Interface`, including
 * exit, shutdown, and sub-trees for `ip`, `ipv6`, and `ospfv3`.
 */

#ifndef INTERFACE_COMMANDS_H
#define INTERFACE_COMMANDS_H

#include "configs/registry/interface/InterfaceRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli::execution
{
DEFINE_CMD_EXECUTOR(Interface, CliMode::Interface, config::InterfaceRegistry);
}

#endif // INTERFACE_COMMANDS_H
