/**
 * @file InterfaceOspfCommands.h
 * @brief Shared OSPF interface command base used by both OSPFv2 and OSPFv3 sub-trees.
 *
 * Defines the common set of OSPF interface-level commands shared between the
 * `ip ospf` (OSPFv2) and `ipv6 ospf` / `ospfv3` (OSPFv3) sub-trees in
 * `CliMode::Interface`.  Covers BFD, interface cost, database filter, dead/hello
 * intervals, demand circuit, flood reduction, MTU ignore, network type, DR
 * priority, retransmit interval, and transmit delay.
 */

#ifndef INTERFACE_OSPF_COMMANDS_H
#define INTERFACE_OSPF_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/Command.hpp"
#include "cli/modes/contexts/InterfaceContext.hpp"

namespace cli
{
bool InterfaceOspf_BFD_Handler(INTERFACE_PARAMS);
using InterfaceOspf_BFD = commandAdder<InterfaceContext,
    InterfaceOspf_BFD_Handler,
    "bfd"_tok, ARG_REST
>;

bool InterfaceOspf_Cost_Handler(INTERFACE_PARAMS);
using InterfaceOspf_Cost = commandAdder<InterfaceContext,
    InterfaceOspf_Cost_Handler,
    "cost"_tok, ARG
>;

bool InterfaceOspf_DatabaseFilter_Handler(INTERFACE_PARAMS);
using InterfaceOspf_DatabaseFilter = commandAdder<InterfaceContext,
    InterfaceOspf_DatabaseFilter_Handler,
    "database-filter"_tok, ARG_REST
>;

bool InterfaceOspf_DeadInterval_Handler(INTERFACE_PARAMS);
using InterfaceOspf_DeadInterval = commandAdder<InterfaceContext,
    InterfaceOspf_DeadInterval_Handler,
    "dead-interval"_tok, ARG_REST
>;

bool InterfaceOspf_DemandCircuit_Handler(INTERFACE_PARAMS);
using InterfaceOspf_DemandCircuit = commandAdder<InterfaceContext,
    InterfaceOspf_DemandCircuit_Handler,
    "demand-circuit"_tok
>;

bool InterfaceOspf_FloodReduction_Handler(INTERFACE_PARAMS);
using InterfaceOspf_FloodReduction = commandAdder<InterfaceContext,
    InterfaceOspf_FloodReduction_Handler,
    "flood-reduction"_tok
>;

bool InterfaceOspf_HelloInterval_Handler(INTERFACE_PARAMS);
using InterfaceOspf_HelloInterval = commandAdder<InterfaceContext,
    InterfaceOspf_HelloInterval_Handler,
    "hello-interval"_tok, ARG_REST
>;

bool InterfaceOspf_MtuIgnore_Handler(INTERFACE_PARAMS);
using InterfaceOspf_MtuIgnore = commandAdder<InterfaceContext,
    InterfaceOspf_MtuIgnore_Handler,
    "mtu-ignore"_tok
>;

bool InterfaceOspf_Network_Handler(INTERFACE_PARAMS);
using InterfaceOspf_Network = commandAdder<InterfaceContext,
    InterfaceOspf_Network_Handler,
    "network"_tok, ARG_REST
>;

bool InterfaceOspf_Priority_Handler(INTERFACE_PARAMS);
using InterfaceOspf_Priority = commandAdder<InterfaceContext,
    InterfaceOspf_Priority_Handler,
    "priority"_tok, ARG_REST
>;

bool InterfaceOspf_RetransmitInterval_Handler(INTERFACE_PARAMS);
using InterfaceOspf_RetransmitInterval = commandAdder<InterfaceContext,
    InterfaceOspf_RetransmitInterval_Handler,
    "retransmit-interval"_tok, ARG_REST
>;

bool InterfaceOspf_TransmitDelay_Handler(INTERFACE_PARAMS);
using InterfaceOspf_TransmitDelay = commandAdder<InterfaceContext,
    InterfaceOspf_TransmitDelay_Handler,
    "transmit-delay"_tok, ARG_REST
>;

/**
 * @brief Shared base parser for common OSPF interface commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Used as a component by both `InterfaceIPOspfCommands` (OSPFv2) and
 * `InterfaceIPv6OspfCommands` / `InterfaceOspfv3Commands` (OSPFv3).
 * Covers `CliMode::Interface` with `InterfaceContext`.
 */
using InterfaceOspfCommands = CliModeParser<CliMode::Interface, InterfaceContext,
    InterfaceOspf_BFD,
    InterfaceOspf_Cost,
    InterfaceOspf_DatabaseFilter,
    InterfaceOspf_DeadInterval,
    InterfaceOspf_DemandCircuit,
    InterfaceOspf_FloodReduction,
    InterfaceOspf_HelloInterval,
    InterfaceOspf_MtuIgnore,
    InterfaceOspf_Network,
    InterfaceOspf_Priority,
    InterfaceOspf_RetransmitInterval,
    InterfaceOspf_TransmitDelay
>;
}

#endif
