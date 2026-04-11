/**
 * @file InterfaceIPCommands.h
 * @brief CLI parser for the `ip` sub-tree of Interface Configuration mode.
 *
 * Defines commands reachable via `ip <...>` in `CliMode::Interface`, covering
 * IPv4 address assignment, EIGRP per-interface parameters (authentication,
 * bandwidth, dampening, hello/hold timers, MTU, next-hop-self, split-horizon,
 * summary-address), and the `ip ospf` sub-tree.
 */

#ifndef INTERFACE_IP_COMMANDS_H
#define INTERFACE_IP_COMMANDS_H

#include "configs/registry/interface/InterfaceRegistry.h"
#include "interface/configs/InterfaceType.hpp"
#include "cli/parser/CliModeParser.hpp"

#define INTERFACE_PARAMS DEFINE_PARAMS(config::InterfaceRegistry)
#define INTERFACE_SUB_PARAMS DEFINE_SUB_PARAMS(config::InterfaceRegistry)

namespace cli
{
bool InterfaceIP_AddressSet_Handler(INTERFACE_PARAMS);
bool InterfaceIP_AddressDhcp_Handler(INTERFACE_PARAMS);
bool InterfaceIP_AuthenticationKeyChain_Handler(INTERFACE_PARAMS);
bool InterfaceIP_AuthenticationMode_Handler(INTERFACE_PARAMS);
bool InterfaceIP_BandwidthPercentage_Handler(INTERFACE_PARAMS);
bool InterfaceIP_DampeningChange_Handler(INTERFACE_PARAMS);
bool InterfaceIP_DampeningInterval_Handler(INTERFACE_PARAMS);
bool InterfaceIP_HelloInterval_Handler(INTERFACE_PARAMS);
bool InterfaceIP_HoldTime_Handler(INTERFACE_PARAMS);
bool InterfaceIP_Mtu_Handler(INTERFACE_PARAMS);
bool InterfaceIP_NextHopSelf_Handler(INTERFACE_PARAMS);
bool InterfaceIP_SplitHorizon_Handler(INTERFACE_PARAMS);
bool InterfaceIP_SummaryAddress_Handler(INTERFACE_PARAMS);

bool InterfaceIP_Ospf_SubHandler(INTERFACE_SUB_PARAMS);

#define INTERFACE_IP_LIST(X, Y) \
    X(Y, (COMMAND, AddressSet, "address"_tok, P_IPV4)) \
    X(Y, (COMMAND, AddressDhcp, "address"_tok, "dhcp"_tok)) \
    X(Y, (COMMAND, AuthenticationKeyChain, "authentication"_tok, "key-chain"_tok)) \
    X(Y, (COMMAND, AuthenticationMode, "authentication"_tok, "mode"_tok)) \
    X(Y, (COMMAND, BandwidthPercentage, "bandwidth-percentage"_tok)) \
    X(Y, (COMMAND, DampeningChange, "dampening-change"_tok)) \
    X(Y, (COMMAND, DampeningInterval, "dampening-interval"_tok)) \
    X(Y, (COMMAND, HelloInterval, "hello-interval"_tok)) \
    X(Y, (COMMAND, HoldTime, "hold-time"_tok)) \
    X(Y, (COMMAND, Mtu, "mtu"_tok)) \
    X(Y, (COMMAND, NextHopSelf, "next-hop-self"_tok)) \
    X(Y, (SUBPRSR, Ospf, "ospf"_tok)) \
    X(Y, (COMMAND, SplitHorizon, "split-horizon"_tok)) \
    X(Y, (COMMAND, SummaryAddress, "summary-address"_tok))

/**
 * @brief Parser for the `ip` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::Interface` with `InterfaceContext` and composes all
 * IPv4 address, EIGRP per-interface, and OSPF interface sub-tree commands.
 */
DEFINE_CMD_MODE(InterfaceIP, CliMode::Interface, config::InterfaceRegistry, INTERFACE_IP_LIST);
}

#undef INTERFACE_IP_LIST
#undef INTERFACE_PARAMS
#undef INTERFACE_SUB_PARAMS

#endif // INTERFACE_IP_COMMANDS_H
