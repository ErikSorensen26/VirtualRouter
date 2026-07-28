/**
 * @file InterfaceIPv6NDCommands.h
 * @brief CLI parser for the `ipv6 nd` sub-tree of Interface Configuration mode.
 *
 * Defines per-interface IPv6 Neighbor Discovery commands reachable via
 * `ipv6 nd <...>` in `CliMode::Interface`, including Router Advertisement
 * tuning, DAD configuration, cache limits, NUD behaviour, destination guard,
 * and autoconfiguration controls.
 */

#ifndef INTERFACE_IPV6_ND_COMMANDS_H
#define INTERFACE_IPV6_ND_COMMANDS_H

#include "configs/registry/interface/NdpRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

#define NDP_PARAMS DEFINE_PARAMS(config::NdpRegistry)

namespace cli::execution
{
DEFINE_CMD_EXECUTOR(InterfaceIPv6ND, CliMode::Interface, config::NdpRegistry);
}

#undef INTERFACE_IPV6_ND_LIST
#undef NDP_PARAMS

#endif // INTERFACE_IPV6_ND_COMMANDS_H
