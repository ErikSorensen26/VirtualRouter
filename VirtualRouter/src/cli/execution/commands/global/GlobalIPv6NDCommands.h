/**
 * @file GlobalIPv6NDCommands.h
 * @brief CLI parser for the `ipv6 nd` sub-tree of Global Configuration mode.
 *
 * Defines global Neighbor Discovery (ND) tuning commands reachable via
 * `ipv6 nd <...>` in `CliMode::GlobalConfiguration`, covering neighbor cache
 * expiry, DAD timers, host-mode, NSF convergence/throttle, NUD limits,
 * reachable-time, resolution data limits, and route-owner behaviour.
 */

#ifndef GLOBAL_IPV6_ND_COMMANDS_H
#define GLOBAL_IPV6_ND_COMMANDS_H

#include "configs/registry/interface/NdpRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli::execution
{
DEFINE_CMD_EXECUTOR(GlobalIPv6ND, CliMode::GlobalConfiguration, config::NdpBaseRegistry);
}

#endif // GLOBAL_IPV6_ND_COMMANDS_H
