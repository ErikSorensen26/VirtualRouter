// GlobalCommands.hpp

#ifndef GLOBAL_COMMANDS_H
#define GLOBAL_COMMANDS_H

#include <CliModeParser.hpp>
#include <GlobalContext.hpp>

namespace Cli
{
bool Global_Arp_Handler(GLOBAL_PARAMS);
using Global_Arp = commandAdder<GlobalContext,
    Global_Arp_Handler,
    "arp"_tok, ARG_REST
>;

bool Global_Exit_Handler(GLOBAL_PARAMS);
using Global_Exit = commandAdder<GlobalContext,
    Global_Exit_Handler,
    "exit"_tok
>;

bool Global_SetHostname_Handler(GLOBAL_PARAMS);
using Global_SetHostname = commandAdder<GlobalContext,
    Global_SetHostname_Handler,
    "hostname"_tok, ARG_REST
>;

bool Global_End_Handler(GLOBAL_PARAMS);
using Global_End = commandAdder<GlobalContext,
    Global_End_Handler,
    "end"_tok
>;

bool Global_IP_Handler(GLOBAL_PARAMS);
using Global_IP = commandAdder<GlobalContext,
    Global_IP_Handler,
    "ip"_tok
>;

bool Global_IPv6_Handler(GLOBAL_PARAMS);
using Global_IPv6 = commandAdder<GlobalContext,
    Global_IPv6_Handler,
    "ipv6"_tok
>;

bool Global_Interface_Handler(GLOBAL_PARAMS);
using Global_Interface = commandAdder<GlobalContext,
    Global_Interface_Handler,
    "interface"_tok, ARG, ARG
>;

bool Global_RouterEIGRP_Handler(GLOBAL_PARAMS);
using Global_RouterEIGRP = commandAdder<GlobalContext,
    Global_RouterEIGRP_Handler,
    "router"_tok, "eigrp"_tok, ARG
>;

bool Global_RouterOSPF_Handler(GLOBAL_PARAMS);
using Global_RouterOSPF = commandAdder<GlobalContext,
    Global_RouterOSPF_Handler,
    "router"_tok, "ospf"_tok, ARG
>;

bool Global_RouterBGP_Handler(GLOBAL_PARAMS);
using Global_RouterBGP = commandAdder<GlobalContext,
    Global_RouterBGP_Handler,
    "router"_tok, "bgp"_tok, ARG
>;

using GlobalCommands = CliModeParser<GlobalContext,
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
