/**
 * @file GlobalIPv6Commands.h
 * @brief CLI parser for the `ipv6` sub-tree of Global Configuration mode.
 *
 * Defines commands reachable via `ipv6 <...>` in `CliMode::GlobalConfiguration`,
 * including Neighbor Discovery (`nd`) settings, static IPv6 neighbor entries,
 * and the entry point for IPv6 EIGRP process configuration.
 */

#ifndef GLOBAL_IPV6_COMMANDS_H
#define GLOBAL_IPV6_COMMANDS_H

#include "configs/registry/global/GlobalRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(GlobalIPv6, CliMode::GlobalConfiguration, config::GlobalRegistry);
}

#endif // GLOBAL_IPV6_COMMANDS_H
