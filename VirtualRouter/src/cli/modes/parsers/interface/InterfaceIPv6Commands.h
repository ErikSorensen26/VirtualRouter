/**
 * @file InterfaceIPv6Commands.h
 * @brief CLI parser for the `ipv6` sub-tree of Interface Configuration mode.
 *
 * Defines commands reachable via `ipv6 <...>` in `CliMode::Interface`, covering
 * IPv6 address assignment, EIGRP per-interface parameters (authentication,
 * bandwidth, dampening, hello/hold timers, MTU, next-hop-self, split-horizon,
 * summary-address), NDP redirects, and sub-trees for `nd` and `ospf`.
 */

#ifndef INTERFACE_IPV6_COMMANDS_H
#define INTERFACE_IPV6_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/SubCommand.hpp"
#include "InterfaceIPv6NDCommands.h"
#include "InterfaceIPv6OspfCommands.h"

namespace cli
{
bool InterfaceIPv6_AddressSet_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_AddressSet = commandAdder<InterfaceContext,
    InterfaceIPv6_AddressSet_Handler,
    "address"_tok
>;

bool InterfaceIPv6_AuthenticationKeyChain_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_AuthenticationKeyChain = commandAdder<InterfaceContext,
    InterfaceIPv6_AuthenticationKeyChain_Handler,
    "authentication"_tok, "key-chain"_tok
>;

bool InterfaceIPv6_AuthenticationMode_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_AuthenticationMode = commandAdder<InterfaceContext,
    InterfaceIPv6_AuthenticationMode_Handler,
    "authentication"_tok, "mode"_tok
>;

bool InterfaceIPv6_BandwidthPercent_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_BandwidthPercent = commandAdder<InterfaceContext,
    InterfaceIPv6_BandwidthPercent_Handler,
    "bandwidth-percent"_tok
>;

bool InterfaceIPv6_DampeningChange_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_DampeningChange = commandAdder<InterfaceContext,
    InterfaceIPv6_DampeningChange_Handler,
    "dampening-change"_tok
>;

bool InterfaceIPv6_DampeningInterval_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_DampeningInterval = commandAdder<InterfaceContext,
    InterfaceIPv6_DampeningInterval_Handler,
    "dampening-interval"_tok
>;

bool InterfaceIPv6_EigrpAs_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_EigrpAs = commandAdder<InterfaceContext,
    InterfaceIPv6_EigrpAs_Handler,
    "eigrp"_tok
>;

bool InterfaceIPv6_HelloInterval_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_HelloInterval = commandAdder<InterfaceContext,
    InterfaceIPv6_HelloInterval_Handler,
    "hello-interval"_tok
>;

bool InterfaceIPv6_HoldTime_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_HoldTime = commandAdder<InterfaceContext,
    InterfaceIPv6_HoldTime_Handler,
    "hold-time"_tok
>;

bool InterfaceIPv6_Mtu_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_Mtu = commandAdder<InterfaceContext,
    InterfaceIPv6_Mtu_Handler,
    "mtu"_tok
>;

using InterfaceIPv6_ND = subAdder<InterfaceContext,
    InterfaceIPv6NDCommands,
    "nd"_tok
>;

bool InterfaceIPv6_NextHopSelf_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_NextHopSelf = commandAdder<InterfaceContext,
    InterfaceIPv6_NextHopSelf_Handler,
    "next-hop-self"_tok
>;

bool InterfaceIPv6_NdpRedirects_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_NdpRedirects = commandAdder<InterfaceContext,
    InterfaceIPv6_NdpRedirects_Handler,
    "redirects"_tok
>;

using InterfaceIPv6_Ospf = subAdder<InterfaceContext,
    InterfaceIPv6OspfCommands,
    "ospf"_tok
>;

bool InterfaceIPv6_SplitHorizon_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_SplitHorizon = commandAdder<InterfaceContext,
    InterfaceIPv6_SplitHorizon_Handler,
    "split-horizon"_tok
>;

bool InterfaceIPv6_SummaryAddress_Handler(INTERFACE_PARAMS);
using InterfaceIPv6_SummaryAddress = commandAdder<InterfaceContext,
    InterfaceIPv6_SummaryAddress_Handler,
    "summary-address"_tok
>;

/**
 * @brief Parser for the `ipv6` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::Interface` with `InterfaceContext` and composes all
 * IPv6 address, EIGRP per-interface, NDP, and OSPFv3 interface sub-tree commands.
 */
using InterfaceIPv6Commands = CliModeParser<CliMode::Interface, InterfaceContext,
    InterfaceIPv6_AddressSet,
    InterfaceIPv6_AuthenticationKeyChain,
    InterfaceIPv6_AuthenticationMode,
    InterfaceIPv6_BandwidthPercent,
    InterfaceIPv6_DampeningChange,
    InterfaceIPv6_DampeningInterval,
    InterfaceIPv6_EigrpAs,
    InterfaceIPv6_HelloInterval,
    InterfaceIPv6_HoldTime,
    InterfaceIPv6_Mtu,
    InterfaceIPv6_ND,
    InterfaceIPv6_NextHopSelf,
    InterfaceIPv6_NdpRedirects,
    InterfaceIPv6_SplitHorizon,
    InterfaceIPv6_SummaryAddress
>;
}

#endif // INTERFACE_IPV6_COMMANDS_H
