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

#include "cli/parser/CliModeParser.hpp"
#include "configs/registry/global/GlobalRegistry.h"

#define GLOBAL_PARAMS DEFINE_PARAMS(config::GlobalRegistry)

namespace cli
{
bool GlobalIPv6ND_CacheExpire_Handler(GLOBAL_PARAMS);
bool GlobalIPv6ND_CacheIntLimit_Handler(GLOBAL_PARAMS);
bool GlobalIPv6ND_DADTime_Handler(GLOBAL_PARAMS);
bool GlobalIPv6ND_HostMode_Handler(GLOBAL_PARAMS);
bool GlobalIPv6ND_NSF_Handler(GLOBAL_PARAMS);
bool GlobalIPv6ND_NudLimit_Handler(GLOBAL_PARAMS);
bool GlobalIPv6ND_ReachableTime_Handler(GLOBAL_PARAMS);
bool GlobalIPv6ND_ResolutionLimit_Handler(GLOBAL_PARAMS);
bool GlobalIPv6ND_RouteOwner_Handler(GLOBAL_PARAMS);

#define GLOBAL_IPV6_ND_LIST(X, Y) \
    X(Y, (_COM_, CacheExpire, "cache"_tok, "expire"_tok)) \
    X(Y, (_COM_, CacheIntLimit, "cache"_tok, "interface-limit"_tok)) \
    X(Y, (_COM_, DADTime, "dad"_tok, "time"_tok)) \
    X(Y, (_COM_, HostMode, "host"_tok, "mode"_tok, "strict"_tok)) \
    X(Y, (_COM_, NSF, "nsf"_tok)) \
    X(Y, (_COM_, NudLimit, "nud"_tok, "limit"_tok)) \
    X(Y, (_COM_, ReachableTime, "reachable-time"_tok)) \
    X(Y, (_COM_, ResolutionLimit, "resolution"_tok, "data"_tok, "limit"_tok)) \
    X(Y, (_COM_, RouteOwner, "route-owner"_tok)) \

DEFINE_CMD_MODE(GlobalIPv6ND, CliMode::GlobalConfiguration, config::GlobalRegistry, GLOBAL_IPV6_ND_LIST)
}

#undef GLOBAL_IPV6_ND_LIST
#undef GLOBAL_PARAMS

#endif // GLOBAL_IPV6_ND_COMMANDS_H
