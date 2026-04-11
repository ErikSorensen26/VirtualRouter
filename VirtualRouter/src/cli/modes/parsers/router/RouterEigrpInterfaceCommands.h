/**
 * @file RouterEigrpInterfaceCommands.h
 * @brief CLI parser for EIGRP interface-level commands.
 *
 * Defines interface-specific EIGRP commands including delay, bandwidth,
reliability, hello interval, hold time, and split horizon settings.
 */

#ifndef ROUTER_EIGRP_INTERFACE_COMMANDS_H
#define ROUTER_EIGRP_INTERFACE_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/modes/contexts/Context.hpp"
#include "configs/registry/router/EigrpInterfaceRegistry.h"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpInterfaceRegistry)

namespace cli
{
bool RouterEigrpInterface_AuthenticationKeyChain_Handler(EIGRP_PARAMS);
bool RouterEigrpInterface_AuthenticationMode_Handler(EIGRP_PARAMS);
bool RouterEigrpInterface_BandwidthPercentage_Handler(EIGRP_PARAMS);
bool RouterEigrpInterface_DampeningChange_Handler(EIGRP_PARAMS);
bool RouterEigrpInterface_DampeningInterval_Handler(EIGRP_PARAMS);
bool RouterEigrpInterface_HelloInterval_Handler(EIGRP_PARAMS);
bool RouterEigrpInterface_HoldTime_Handler(EIGRP_PARAMS);
bool RouterEigrpInterface_NextHopSelf_Handler(EIGRP_PARAMS);
bool RouterEigrpInterface_PassiveInterface_Handler(EIGRP_PARAMS);
// bool RouterEigrpInterface_Shutdown_Handler(EIGRP_PARAMS); //TODO
bool RouterEigrpInterface_SplitHorizon_Handler(EIGRP_PARAMS);
bool RouterEigrpInterface_SummaryAddress_Handler(EIGRP_PARAMS);

bool RouterEigrpInterfaceV4_Exit_Handler(EIGRP_PARAMS);
bool RouterEigrpInterfaceV6_Exit_Handler(EIGRP_PARAMS);

#define ROUTER_EIGRP_INTERFACE_LIST(X, Y) \
    X(Y, (_COM_, AuthenticationKeyChain, "authentication"_tok, "key-chain"_tok)) \
    X(Y, (_COM_, AuthenticationMode, "authentication"_tok, "mode"_tok)) \
    X(Y, (_COM_, BandwidthPercentage, "bandwidth-percentage"_tok)) \
    X(Y, (_COM_, DampeningChange, "dampening-change"_tok)) \
    X(Y, (_COM_, DampeningInterval, "dampening-interval"_tok)) \
    X(Y, (_COM_, HelloInterval, "hello-interval"_tok)) \
    X(Y, (_COM_, HoldTime, "hold-time"_tok)) \
    X(Y, (_COM_, NextHopSelf, "next-hop-self"_tok)) \
    X(Y, (_COM_, PassiveInterface, "passive-interface"_tok)) \
    X(Y, (_COM_, SplitHorizon, "split-horizon"_tok)) \
    X(Y, (_COM_, SummaryAddress, "summary-address"_tok))

/**
 * @brief Parser for EIGRPv4 interface-level configuration commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Configures per-interface EIGRP parameters including bandwidth, delay,
 * reliability, timers, and split horizon settings.
 */
DEFINE_CMD_MODE(RouterEigrpInterface, CliMode::None, config::EigrpInterfaceRegistry, ROUTER_EIGRP_INTERFACE_LIST)

#define ROUTER_EIGRP_INTERFACE_LIST_V4(X, Y) \
    X(Y, (_COM_, Exit, "exit-af-intervace"_tok)) \
    X(Y, (_EXT_, RouterEigrpInterfaceCommands))

/**
 * @brief IPv4 address-family interface mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpInterfaceV4, CliMode::RouterEigrpInterfaceV4, config::EigrpInterfaceRegistry, ROUTER_EIGRP_INTERFACE_LIST_V4)

#define ROUTER_EIGRP_INTERFACE_LIST_V6(X, Y) \
    X(Y, (_COM_, Exit, "exit-af-intervace"_tok)) \
    X(Y, (_EXT_, RouterEigrpInterfaceCommands))

/**
 * @brief IPv6 address-family interface mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpInterfaceV6, CliMode::RouterEigrpInterfaceV6, config::EigrpInterfaceRegistry, ROUTER_EIGRP_INTERFACE_LIST_V6)
}

#undef ROUTER_EIGRP_INTERFACE_LIST
#undef ROUTER_EIGRP_INTERFACE_LIST_V4
#undef ROUTER_EIGRP_INTERFACE_LIST_V6
#undef EIGRP_PARAMS

#endif // ROUTER_EIGRP_INTERFACE_COMMANDS_H
