// InterfaceIPCommands.cpp

#ifndef INTERFACE_IP_COMMANDS_H
#define INTERFACE_IP_COMMANDS_H

#include <CliModeParser.hpp>
#include <InterfaceContext.hpp>
#include "InterfaceIPOspfCommands.h"

namespace Cli
{
bool InterfaceIP_AddressSet_Handler(INTERFACE_PARAMS);
using InterfaceIP_AddressSet = commandAdder<InterfaceContext,
    InterfaceIP_AddressSet_Handler,
    "address"_tok, ARG_REST
>;

bool InterfaceIP_AuthenticationKeyChain_Handler(INTERFACE_PARAMS);
using InterfaceIP_AuthenticationKeyChain = commandAdder<InterfaceContext,
    InterfaceIP_AuthenticationKeyChain_Handler,
    "authentication"_tok, "key-chain"_tok, ARG_REST
>;

bool InterfaceIP_AuthenticationMode_Handler(INTERFACE_PARAMS);
using InterfaceIP_AuthenticationMode = commandAdder<InterfaceContext,
    InterfaceIP_AuthenticationMode_Handler,
    "authentication"_tok, "mode"_tok, ARG_REST
>;

bool InterfaceIP_BandwidthPercentage_Handler(INTERFACE_PARAMS);
using InterfaceIP_BandwidthPercentage = commandAdder<InterfaceContext,
    InterfaceIP_BandwidthPercentage_Handler,
    "bandwidth-percentage"_tok, ARG_REST
>;

bool InterfaceIP_DampeningChange_Handler(INTERFACE_PARAMS);
using InterfaceIP_DampeningChange = commandAdder<InterfaceContext,
    InterfaceIP_DampeningChange_Handler,
    "dampening-change"_tok, ARG_REST
>;

bool InterfaceIP_DampeningInterval_Handler(INTERFACE_PARAMS);
using InterfaceIP_DampeningInterval = commandAdder<InterfaceContext,
    InterfaceIP_DampeningInterval_Handler,
    "dampening-interval"_tok, ARG_REST
>;

bool InterfaceIP_HelloInterval_Handler(INTERFACE_PARAMS);
using InterfaceIP_HelloInterval = commandAdder<InterfaceContext,
    InterfaceIP_HelloInterval_Handler,
    "hello-interval"_tok, ARG_REST
>;

bool InterfaceIP_HoldTime_Handler(INTERFACE_PARAMS);
using InterfaceIP_HoldTime = commandAdder<InterfaceContext,
    InterfaceIP_HoldTime_Handler,
    "hold-time"_tok, ARG_REST
>;

bool InterfaceIP_Mtu_Handler(INTERFACE_PARAMS);
using InterfaceIP_Mtu = commandAdder<InterfaceContext,
    InterfaceIP_Mtu_Handler,
    "mtu"_tok, ARG_REST
>;

bool InterfaceIP_NextHopSelf_Handler(INTERFACE_PARAMS);
using InterfaceIP_NextHopSelf = commandAdder<InterfaceContext,
    InterfaceIP_NextHopSelf_Handler,
    "next-hop-self"_tok, ARG_REST
>;

using InterfaceIP_Ospf = subAdder<InterfaceContext,
    InterfaceIPOspfCommands, 
    "ospf"_tok
>;

bool InterfaceIP_SplitHorizon_Handler(INTERFACE_PARAMS);
using InterfaceIP_SplitHorizon = commandAdder<InterfaceContext,
    InterfaceIP_SplitHorizon_Handler,
    "split-horizon"_tok, ARG_REST
>;

bool InterfaceIP_SummaryAddress_Handler(INTERFACE_PARAMS);
using InterfaceIP_SummaryAddress = commandAdder<InterfaceContext,
    InterfaceIP_SummaryAddress_Handler,
    "summary-address"_tok, ARG_REST
>;

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
