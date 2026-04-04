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

#include "cli/parser/SubCommand.hpp"
#include "InterfaceIPOspfCommands.h"

namespace cli
{
bool InterfaceIP_AddressSet_Handler(INTERFACE_PARAMS);
using InterfaceIP_AddressSet = commandAdder<InterfaceContext,
    InterfaceIP_AddressSet_Handler,
    "address"_tok
>;

bool InterfaceIP_AuthenticationKeyChain_Handler(INTERFACE_PARAMS);
using InterfaceIP_AuthenticationKeyChain = commandAdder<InterfaceContext,
    InterfaceIP_AuthenticationKeyChain_Handler,
    "authentication"_tok, "key-chain"_tok
>;

bool InterfaceIP_AuthenticationMode_Handler(INTERFACE_PARAMS);
using InterfaceIP_AuthenticationMode = commandAdder<InterfaceContext,
    InterfaceIP_AuthenticationMode_Handler,
    "authentication"_tok, "mode"_tok
>;

bool InterfaceIP_BandwidthPercentage_Handler(INTERFACE_PARAMS);
using InterfaceIP_BandwidthPercentage = commandAdder<InterfaceContext,
    InterfaceIP_BandwidthPercentage_Handler,
    "bandwidth-percentage"_tok
>;

bool InterfaceIP_DampeningChange_Handler(INTERFACE_PARAMS);
using InterfaceIP_DampeningChange = commandAdder<InterfaceContext,
    InterfaceIP_DampeningChange_Handler,
    "dampening-change"_tok
>;

bool InterfaceIP_DampeningInterval_Handler(INTERFACE_PARAMS);
using InterfaceIP_DampeningInterval = commandAdder<InterfaceContext,
    InterfaceIP_DampeningInterval_Handler,
    "dampening-interval"_tok
>;

bool InterfaceIP_HelloInterval_Handler(INTERFACE_PARAMS);
using InterfaceIP_HelloInterval = commandAdder<InterfaceContext,
    InterfaceIP_HelloInterval_Handler,
    "hello-interval"_tok
>;

bool InterfaceIP_HoldTime_Handler(INTERFACE_PARAMS);
using InterfaceIP_HoldTime = commandAdder<InterfaceContext,
    InterfaceIP_HoldTime_Handler,
    "hold-time"_tok
>;

bool InterfaceIP_Mtu_Handler(INTERFACE_PARAMS);
using InterfaceIP_Mtu = commandAdder<InterfaceContext,
    InterfaceIP_Mtu_Handler,
    "mtu"_tok
>;

bool InterfaceIP_NextHopSelf_Handler(INTERFACE_PARAMS);
using InterfaceIP_NextHopSelf = commandAdder<InterfaceContext,
    InterfaceIP_NextHopSelf_Handler,
    "next-hop-self"_tok
>;

using InterfaceIP_Ospf = subAdder<InterfaceContext,
    InterfaceIPOspfCommands,
    "ospf"_tok
>;

bool InterfaceIP_SplitHorizon_Handler(INTERFACE_PARAMS);
using InterfaceIP_SplitHorizon = commandAdder<InterfaceContext,
    InterfaceIP_SplitHorizon_Handler,
    "split-horizon"_tok
>;

bool InterfaceIP_SummaryAddress_Handler(INTERFACE_PARAMS);
using InterfaceIP_SummaryAddress = commandAdder<InterfaceContext,
    InterfaceIP_SummaryAddress_Handler,
    "summary-address"_tok
>;

/**
 * @brief Parser for the `ip` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::Interface` with `InterfaceContext` and composes all
 * IPv4 address, EIGRP per-interface, and OSPF interface sub-tree commands.
 */
using InterfaceIPCommands = CliModeParser<CliMode::Interface, InterfaceContext,
    InterfaceIP_AddressSet,
    InterfaceIP_AuthenticationKeyChain,
    InterfaceIP_AuthenticationMode,
    InterfaceIP_BandwidthPercentage,
    InterfaceIP_DampeningChange,
    InterfaceIP_DampeningInterval,
    InterfaceIP_HelloInterval,
    InterfaceIP_HoldTime,
    InterfaceIP_Mtu,
    InterfaceIP_NextHopSelf,
    InterfaceIP_Ospf,
    InterfaceIP_SplitHorizon,
    InterfaceIP_SummaryAddress
>;
}

#endif // INTERFACE_IP_COMMANDS_H
