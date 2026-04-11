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
#include "cli/modes/contexts/Context.hpp"
#include "configs/registry/global/GlobalRegistry.h"

#define GLOBAL_PARAMS DEFINE_PARAMS(config::GlobalRegistry)
#define GLOBAL_SUB_PARAMS DEFINE_SUB_PARAMS(config::GlobalRegistry)

namespace cli
{
bool Global_Arp_Handler(GLOBAL_PARAMS);
bool Global_Exit_Handler(GLOBAL_PARAMS);
bool Global_SetHostname_Handler(GLOBAL_PARAMS);
bool Global_End_Handler(GLOBAL_PARAMS);
bool Global_Interface_Handler(GLOBAL_PARAMS);
bool Global_RouterEIGRP_Handler(GLOBAL_PARAMS);
bool Global_RouterOSPF_Handler(GLOBAL_PARAMS);
bool Global_RouterBGP_Handler(GLOBAL_PARAMS);

bool Global_IP_SubHandler(GLOBAL_SUB_PARAMS);
bool Global_IPv6_SubHandler(GLOBAL_SUB_PARAMS);

#define GLOBAL_LIST(X, Y) \
    X(Y, (_COM_, Arp, "arp"_tok)) \
    X(Y, (_COM_, Exit, "exit"_tok)) \
    X(Y, (_COM_, SetHostname, "hostname"_tok)) \
    X(Y, (_COM_, End, "end"_tok)) \
    X(Y, (_SUB_, IP, "ip"_tok)) \
    X(Y, (_SUB_, IPv6, "ipv6"_tok)) \
    X(Y, (_COM_, Interface, "interface"_tok)) \
    X(Y, (_COM_, RouterEIGRP, "router"_tok, "eigrp"_tok)) \
    X(Y, (_COM_, RouterOSPF, "router"_tok, "ospf"_tok)) \
    X(Y, (_COM_, RouterBGP, "router"_tok, "bgp"_tok))

DEFINE_CMD_MODE(Global, CliMode::GlobalConfiguration, config::GlobalRegistry, GLOBAL_LIST);
}

#undef GLOBAL_LIST
#undef GLOBAL_PARAMS
#undef GLOBAL_SUB_PARAMS

#endif // GLOBAL_COMMANDS_H
