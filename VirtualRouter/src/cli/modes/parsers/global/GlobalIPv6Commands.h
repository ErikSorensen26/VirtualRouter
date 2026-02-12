// GlobalIPv6Commands.h

#ifndef GLOBAL_IPV6_COMMANDS_H
#define GLOBAL_IPV6_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/Command.hpp"
#include "cli/parser/SubCommand.hpp"
#include "cli/modes/contexts/GlobalContext.hpp"
#include "GlobalIPv6NDCommands.h"

namespace Cli
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

using GlobalIPv6Commands = CliModeParser<CliMode::GlobalConfiguration, GlobalContext,
    GlobalIPv6_ND,
    GlobalIPv6_Neighbor,
    GlobalIPv6_RouterEIGRP
>;
}

#endif // GLOBAL_IPV6_COMMANDS_H
