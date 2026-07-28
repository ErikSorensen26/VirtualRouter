/**
 * @file InterfaceIPCommands.h
 * @brief CLI parser for the `ip` sub-tree of Interface Configuration mode.
 *
 * Defines commands reachable via `ip <...>` in `CliMode::Interface`, covering
 * IPv4 address assignment, EIGRP per-interface parameters (authentication,
 * bandwidth, dampening, hello/hold timers, MTU, next-hop-self, split-horizon,
 * summary-address), and the `ip ospf` sub-tree.
 */

#ifndef INTERFACE_IP_COMMANDS_H
#define INTERFACE_IP_COMMANDS_H

#include "configs/registry/interface/InterfaceRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli::execution
{
DEFINE_CMD_EXECUTOR(InterfaceIP, CliMode::Interface, config::InterfaceRegistry);
}

#endif // INTERFACE_IP_COMMANDS_H
