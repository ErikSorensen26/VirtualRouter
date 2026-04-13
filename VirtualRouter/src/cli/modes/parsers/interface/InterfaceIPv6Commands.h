/**
 * @file InterfaceIPv6Commands.h
 * @brief CLI parser for the `ipv6` sub-tree of Interface Configuration mode.
 *
 * Defines commands reachable via `ipv6 <...>` in `CliMode::Interface`, covering
 * IPv6 address assignment, EIGRP per-interface parameters (authentication,
 * bandwidth, dampening, hello/hold timers, MTU, next-hop-self, split-horizon,
 * summary-address), NDP redirects, and sub-trees for `nd` and `ospf`.
 */

#ifndef INTERFACE_IPV6_COMMANDS_H
#define INTERFACE_IPV6_COMMANDS_H

#include "configs/registry/interface/InterfaceRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(InterfaceIPv6, CliMode::Interface, config::InterfaceRegistry);
}

#endif // INTERFACE_IPV6_COMMANDS_H
