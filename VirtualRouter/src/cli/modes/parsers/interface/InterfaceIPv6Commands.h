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

#include "configs/registry/interface/InterfaceRegistry.h"
#include "interface/configs/InterfaceType.hpp"
#include "cli/parser/CliModeParser.hpp"

#define INTERFACE_PARAMS DEFINE_PARAMS(config::InterfaceRegistry)
#define INTERFACE_SUB_PARAMS DEFINE_SUB_PARAMS(config::InterfaceRegistry)

namespace cli
{
bool InterfaceIPv6_AddressSet_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_AddressNamed_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_AddressLinkLocal_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_AddressAuto_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_AuthenticationKeyChain_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_AuthenticationMode_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_BandwidthPercent_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_DampeningChange_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_DampeningInterval_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_Eigrp_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_HelloInterval_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_HoldTime_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_Mtu_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_NextHopSelf_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_NdpRedirects_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_SplitHorizon_Handler(INTERFACE_PARAMS);
bool InterfaceIPv6_SummaryAddress_Handler(INTERFACE_PARAMS);

bool InterfaceIPv6_ND_SubHandler(INTERFACE_SUB_PARAMS);
bool InterfaceIPv6_Ospf_SubHandler(INTERFACE_SUB_PARAMS);

#define INTERFACE_IPV6_LIST(X, Y) \
    X(Y, (COMMAND, AddressSet, "address"_tok, P_IPV6PFX)) \
    X(Y, (COMMAND, AddressNamed, "address"_tok, P_WORD)) \
    X(Y, (COMMAND, AddressLinkLocal, "address"_tok, P_IPV6)) \
    X(Y, (COMMAND, AddressAuto, "address"_tok, "autoconfig"_tok)) \
    X(Y, (COMMAND, AuthenticationKeyChain, "authentication"_tok, "key-chain"_tok)) \
    X(Y, (COMMAND, AuthenticationMode, "authentication"_tok, "mode"_tok)) \
    X(Y, (COMMAND, BandwidthPercent, "bandwidth-percent"_tok)) \
    X(Y, (COMMAND, DampeningChange, "dampening-change"_tok)) \
    X(Y, (COMMAND, DampeningInterval, "dampening-interval"_tok)) \
    X(Y, (COMMAND, Eigrp, "eigrp"_tok)) \
    X(Y, (COMMAND, HelloInterval, "hello-interval"_tok)) \
    X(Y, (COMMAND, HoldTime, "hold-time"_tok)) \
    X(Y, (COMMAND, Mtu, "mtu"_tok)) \
    X(Y, (SUBPRSR, ND, "nd"_tok)) \
    X(Y, (COMMAND, NextHopSelf, "next-hop-self"_tok)) \
    X(Y, (COMMAND, NdpRedirects, "redirects"_tok)) \
    X(Y, (SUBPRSR, Ospf, "ospf"_tok)) \
    X(Y, (COMMAND, SplitHorizon, "split-horizon"_tok)) \
    X(Y, (COMMAND, SummaryAddress, "summary-address"_tok))

/**
 * @brief Parser for the `ipv6` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::Interface` with `InterfaceContext` and composes all
 * IPv6 address, EIGRP per-interface, NDP, and OSPFv3 interface sub-tree commands.
 */
DEFINE_CMD_MODE(InterfaceIPv6, CliMode::Interface, config::InterfaceRegistry, INTERFACE_IPV6_LIST)
}

#undef INTERFACE_IPV6_LIST
#undef INTERFACE_PARAMS
#undef INTERFACE_SUB_PARAMS

#endif // INTERFACE_IPV6_COMMANDS_H
