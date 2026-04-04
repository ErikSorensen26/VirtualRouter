/**
 * @file GlobalCommands.h
 * @brief Top-level CLI mode parser for the Global Configuration mode.
 *
 * Aggregates all commands available in `CliMode::GlobalConfiguration`, including
 * ARP management, hostname, routing protocol entry points (EIGRP, OSPF, BGP),
 * interface navigation, and sub-trees for `ip` and `ipv6`.
 */

#ifndef GLOBAL_COMMANDS_H
#define GLOBAL_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/Command.hpp"
#include "cli/parser/SubCommand.hpp"
#include "cli/modes/contexts/GlobalContext.hpp"

#include "GlobalIPCommands.h"
#include "GlobalIPv6Commands.h"

namespace cli
{
bool Global_Arp_Handler(GLOBAL_PARAMS);
using Global_Arp = commandAdder<GlobalContext,
    Global_Arp_Handler,
    "arp"_tok
>;

bool Global_Exit_Handler(GLOBAL_PARAMS);
using Global_Exit = commandAdder<GlobalContext,
    Global_Exit_Handler,
    "exit"_tok
>;

bool Global_SetHostname_Handler(GLOBAL_PARAMS);
using Global_SetHostname = commandAdder<GlobalContext,
    Global_SetHostname_Handler,
    "hostname"_tok
>;

bool Global_End_Handler(GLOBAL_PARAMS);
using Global_End = commandAdder<GlobalContext,
    Global_End_Handler,
    "end"_tok
>;

using Global_IP = subAdder<GlobalContext,
    GlobalIPCommands,
    "ip"_tok
>;

using Global_IPv6 = subAdder<GlobalContext,
    GlobalIPv6Commands,
    "ipv6"_tok
>;

bool Global_Interface_Handler(GLOBAL_PARAMS);
using Global_Interface = commandAdder<GlobalContext,
    Global_Interface_Handler,
    "interface"_tok
>;

bool Global_RouterEIGRP_Handler(GLOBAL_PARAMS);
using Global_RouterEIGRP = commandAdder<GlobalContext,
    Global_RouterEIGRP_Handler,
    "router"_tok, "eigrp"_tok
>;

bool Global_RouterOSPF_Handler(GLOBAL_PARAMS);
using Global_RouterOSPF = commandAdder<GlobalContext,
    Global_RouterOSPF_Handler,
    "router"_tok, "ospf"_tok
>;

bool Global_RouterBGP_Handler(GLOBAL_PARAMS);
using Global_RouterBGP = commandAdder<GlobalContext,
    Global_RouterBGP_Handler,
    "router"_tok, "bgp"_tok
>;

/**
 * @brief Complete parser for the Global Configuration CLI mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Instantiated as a `CliModeParser` that covers `CliMode::GlobalConfiguration`
 * with `GlobalContext`. Composes all top-level global commands including
 * ARP, hostname, routing protocol entry points, interface navigation,
 * and `ip`/`ipv6` sub-trees.
 */
using GlobalCommands = CliModeParser<CliMode::GlobalConfiguration, GlobalContext,
    Global_Arp,
    Global_Exit,
    Global_SetHostname,
    Global_End,
    Global_IP,
    Global_IPv6,
    Global_Interface,
    Global_RouterEIGRP,
    Global_RouterOSPF,
    Global_RouterBGP
>;
}


#endif // GLOBAL_COMMANDS_H
