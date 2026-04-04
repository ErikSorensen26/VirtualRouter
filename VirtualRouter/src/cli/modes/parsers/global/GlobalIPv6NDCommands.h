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
#include "cli/parser/Command.hpp"
#include "cli/modes/contexts/GlobalContext.hpp"

namespace cli
{
bool GlobalIPv6ND_CacheExpire_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_CacheExpire = commandAdder<GlobalContext,
    GlobalIPv6ND_CacheExpire_Handler,
    "cache"_tok, "expire"_tok
>;

bool GlobalIPv6ND_CacheIntLimit_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_CacheIntLimit = commandAdder<GlobalContext,
    GlobalIPv6ND_CacheIntLimit_Handler,
    "cache"_tok, "interface-limit"_tok
>;

bool GlobalIPv6ND_DADTime_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_DADTime = commandAdder<GlobalContext,
    GlobalIPv6ND_DADTime_Handler,
    "dad"_tok, "time"_tok
>;

bool GlobalIPv6ND_HostMode_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_HostMode = commandAdder<GlobalContext,
    GlobalIPv6ND_HostMode_Handler,
    "host"_tok, "mode"_tok, "strict"_tok
>;

bool GlobalIPv6ND_NSF_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_NSF = commandAdder<GlobalContext,
    GlobalIPv6ND_NSF_Handler,
    "nsf"_tok
>;

bool GlobalIPv6ND_NudLimit_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_NudLimit = commandAdder<GlobalContext,
    GlobalIPv6ND_NudLimit_Handler,
    "nud"_tok, "limit"_tok
>;

bool GlobalIPv6ND_ReachableTime_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_ReachableTime = commandAdder<GlobalContext,
    GlobalIPv6ND_ReachableTime_Handler,
    "reachable-time"_tok
>;

bool GlobalIPv6ND_ResolutionLimit_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_ResolutionLimit = commandAdder<GlobalContext,
    GlobalIPv6ND_ResolutionLimit_Handler,
    "resolution"_tok, "data"_tok, "limit"_tok
>;

bool GlobalIPv6ND_RouteOwner_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_RouteOwner = commandAdder<GlobalContext,
    GlobalIPv6ND_RouteOwner_Handler,
    "route-owner"_tok
>;

/**
 * @brief Parser for the `ipv6 nd` sub-tree in Global Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::GlobalConfiguration` with `GlobalContext` and exposes
 * all global Neighbor Discovery tuning commands.
 */
using GlobalIPv6NDCommands = CliModeParser<CliMode::GlobalConfiguration, GlobalContext,
    GlobalIPv6ND_CacheExpire,
    GlobalIPv6ND_CacheIntLimit,
    GlobalIPv6ND_DADTime,
    GlobalIPv6ND_HostMode,
    GlobalIPv6ND_NSF,
    GlobalIPv6ND_NudLimit,
    GlobalIPv6ND_ReachableTime,
    GlobalIPv6ND_ResolutionLimit,
    GlobalIPv6ND_RouteOwner
>;
}

#endif // GLOBAL_IPV6_ND_COMMANDS_H
