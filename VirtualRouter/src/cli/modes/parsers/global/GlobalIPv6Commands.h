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

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/Command.hpp"
#include "cli/parser/SubCommand.hpp"
#include "cli/modes/contexts/GlobalContext.hpp"
#include "GlobalIPv6NDCommands.h"

namespace cli
{
using GlobalIPv6_ND = subAdder<GlobalContext,
    GlobalIPv6NDCommands,
    "nd"_tok
>;

bool GlobalIPv6_Neighbor_Handler(GLOBAL_PARAMS);
using GlobalIPv6_Neighbor = commandAdder<GlobalContext,
    GlobalIPv6_Neighbor_Handler,
    "neighbor"_tok, ARG, ARG, ARG, ARG
>;

bool GlobalIPv6_RouterEIGRP_Handler(GLOBAL_PARAMS);
using GlobalIPv6_RouterEIGRP = commandAdder<GlobalContext,
    GlobalIPv6_RouterEIGRP_Handler,
    "router"_tok, "eigrp"_tok, ARG
>;

/**
 * @brief Parser for the `ipv6` sub-tree in Global Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::GlobalConfiguration` with `GlobalContext` and exposes
 * Neighbor Discovery configuration, static neighbor entries, and the
 * IPv6 EIGRP router entry point.
 */
using GlobalIPv6Commands = CliModeParser<CliMode::GlobalConfiguration, GlobalContext,
    GlobalIPv6_ND,
    GlobalIPv6_Neighbor,
    GlobalIPv6_RouterEIGRP
>;
}

#endif // GLOBAL_IPV6_COMMANDS_H
