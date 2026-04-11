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
#include "cli/parser/CliModeParser.hpp"

#define NDP_PARAMS DEFINE_PARAMS(config::NdpBaseRegistry)

namespace cli
{
bool GlobalIPv6ND_CacheExpire_Handler(NDP_PARAMS);
bool GlobalIPv6ND_CacheIntLimit_Handler(NDP_PARAMS);
bool GlobalIPv6ND_DADTime_Handler(NDP_PARAMS);
bool GlobalIPv6ND_HostMode_Handler(NDP_PARAMS);
bool GlobalIPv6ND_NSF_Handler(NDP_PARAMS);
bool GlobalIPv6ND_NudLimit_Handler(NDP_PARAMS);
bool GlobalIPv6ND_ReachableTime_Handler(NDP_PARAMS);
bool GlobalIPv6ND_ResolutionLimit_Handler(NDP_PARAMS);
bool GlobalIPv6ND_RouteOwner_Handler(NDP_PARAMS);

#define GLOBAL_IPV6_ND_LIST(X, Y) \
    X(Y, (COMMAND, CacheExpire, "cache"_tok, "expire"_tok)) \
    X(Y, (COMMAND, CacheIntLimit, "cache"_tok, "interface-limit"_tok)) \
    X(Y, (COMMAND, DADTime, "dad"_tok, "time"_tok)) \
    X(Y, (COMMAND, HostMode, "host"_tok, "mode"_tok, "strict"_tok)) \
    X(Y, (COMMAND, NSF, "nsf"_tok)) \
    X(Y, (COMMAND, NudLimit, "nud"_tok, "limit"_tok)) \
    X(Y, (COMMAND, ReachableTime, "reachable-time"_tok)) \
    X(Y, (COMMAND, ResolutionLimit, "resolution"_tok, "data"_tok, "limit"_tok)) \
    X(Y, (COMMAND, RouteOwner, "route-owner"_tok)) \

DEFINE_CMD_MODE(GlobalIPv6ND, CliMode::GlobalConfiguration, config::NdpBaseRegistry, GLOBAL_IPV6_ND_LIST)
}

#undef GLOBAL_IPV6_ND_LIST
#undef NDP_PARAMS

#endif // GLOBAL_IPV6_ND_COMMANDS_H
