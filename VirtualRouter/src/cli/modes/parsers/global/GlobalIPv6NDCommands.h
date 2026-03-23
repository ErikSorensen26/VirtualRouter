// GlobalIPv6NDCommands.h

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
    "cache"_tok, "expire"_tok, ARG_REST
>;

bool GlobalIPv6ND_CacheIntLimit_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_CacheIntLimit = commandAdder<GlobalContext,
    GlobalIPv6ND_CacheIntLimit_Handler,
    "cache"_tok, "interface-limit"_tok, ARG_REST
>;

bool GlobalIPv6ND_DADTime_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_DADTime = commandAdder<GlobalContext,
    GlobalIPv6ND_DADTime_Handler,
    "dad"_tok, "time"_tok, ARG_REST
>;

bool GlobalIPv6ND_HostMode_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_HostMode = commandAdder<GlobalContext,
    GlobalIPv6ND_HostMode_Handler,
    "host"_tok, "mode"_tok, "strict"_tok
>;

bool GlobalIPv6ND_NSFConvergence_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_NSFConvergence = commandAdder<GlobalContext,
    GlobalIPv6ND_NSFConvergence_Handler,
    "nsf"_tok, "convergence"_tok, ARG_REST
>;

bool GlobalIPv6ND_NSFDADSuppress_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_NSFDADSuppress = commandAdder<GlobalContext,
    GlobalIPv6ND_NSFDADSuppress_Handler,
    "nsf"_tok, "dad"_tok, "suppress"_tok, ARG_REST
>;

bool GlobalIPv6ND_NSFThrottle_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_NSFThrottle = commandAdder<GlobalContext,
    GlobalIPv6ND_NSFThrottle_Handler,
    "nsf"_tok, "throttle"_tok, ARG_REST
>;

bool GlobalIPv6ND_NudLimit_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_NudLimit = commandAdder<GlobalContext,
    GlobalIPv6ND_NudLimit_Handler,
    "nud"_tok, "limit"_tok, ARG_REST
>;

bool GlobalIPv6ND_ReachableTime_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_ReachableTime = commandAdder<GlobalContext,
    GlobalIPv6ND_ReachableTime_Handler,
    "reachable-time"_tok, ARG_REST
>;

bool GlobalIPv6ND_ResolutionLimit_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_ResolutionLimit = commandAdder<GlobalContext,
    GlobalIPv6ND_ResolutionLimit_Handler,
    "resolution"_tok, "data"_tok, "limit"_tok, ARG_REST
>;

bool GlobalIPv6ND_RouteOwner_Handler(GLOBAL_PARAMS);
using GlobalIPv6ND_RouteOwner = commandAdder<GlobalContext,
    GlobalIPv6ND_RouteOwner_Handler,
    "route-owner"_tok
>;

using GlobalIPv6NDCommands = CliModeParser<CliMode::GlobalConfiguration, GlobalContext,
    GlobalIPv6ND_CacheExpire,
    GlobalIPv6ND_CacheIntLimit,
    GlobalIPv6ND_DADTime,
    GlobalIPv6ND_HostMode,
    GlobalIPv6ND_NSFConvergence,
    GlobalIPv6ND_NSFDADSuppress,
    GlobalIPv6ND_NSFThrottle,
    GlobalIPv6ND_NudLimit,
    GlobalIPv6ND_ReachableTime,
    GlobalIPv6ND_ResolutionLimit,
    GlobalIPv6ND_RouteOwner
>;
}

#endif // GLOBAL_IPV6_ND_COMMANDS_H
