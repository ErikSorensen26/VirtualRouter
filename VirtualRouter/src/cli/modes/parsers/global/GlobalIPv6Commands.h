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
#include "cli/parser/CliModeParser.hpp"

#define GLOBAL_PARAMS DEFINE_PARAMS(config::GlobalRegistry)
#define GLOBAL_SUB_PARAMS DEFINE_SUB_PARAMS(config::GlobalRegistry)

namespace cli
{
bool GlobalIPv6_ND_SubHandler(GLOBAL_SUB_PARAMS);
bool GlobalIPv6_Neighbor_Handler(GLOBAL_PARAMS);
bool GlobalIPv6_RouterEIGRP_Handler(GLOBAL_PARAMS);

#define GLOBAL_IPV6_LIST(X, Y) \
    X(Y, (SUBPRSR, ND, "nd"_tok)) \
    X(Y, (COMMAND, Neighbor, "neighbor"_tok)) \
    X(Y, (COMMAND, RouterEIGRP, "router"_tok, "eigrp"_tok))

DEFINE_CMD_MODE(GlobalIPv6, CliMode::GlobalConfiguration, config::GlobalRegistry, GLOBAL_IPV6_LIST);
}

#undef GLOBAL_IPV6_LIST
#undef GLOBAL_PARAMS
#undef GLOBAL_SUB_PARAMS

#endif // GLOBAL_IPV6_COMMANDS_H
