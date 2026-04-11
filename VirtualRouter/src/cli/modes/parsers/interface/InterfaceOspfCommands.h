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
#include "cli/modes/contexts/Context.hpp"
#include "configs/registry/router/OspfInterfaceRegistry.h"

#define OSPF_PARAMS DEFINE_PARAMS(config::OspfInterfaceBaseRegistry)

namespace cli
{
bool InterfaceOspf_BFD_Handler(OSPF_PARAMS);
bool InterfaceOspf_Cost_Handler(OSPF_PARAMS);
bool InterfaceOspf_DatabaseFilter_Handler(OSPF_PARAMS);
bool InterfaceOspf_DeadInterval_Handler(OSPF_PARAMS);
bool InterfaceOspf_DemandCircuit_Handler(OSPF_PARAMS);
bool InterfaceOspf_FloodReduction_Handler(OSPF_PARAMS);
bool InterfaceOspf_HelloInterval_Handler(OSPF_PARAMS);
bool InterfaceOspf_MtuIgnore_Handler(OSPF_PARAMS);
bool InterfaceOspf_Network_Handler(OSPF_PARAMS);
bool InterfaceOspf_Priority_Handler(OSPF_PARAMS);
bool InterfaceOspf_RetransmissionInterval_Handler(OSPF_PARAMS);
bool InterfaceOspf_TransmitDelay_Handler(OSPF_PARAMS);

#define INTERFACE_OSPF_LIST(X, Y) \
    X(Y, (_COM_, BFD, "bfd"_tok)) \
    X(Y, (_COM_, Cost, "cost"_tok)) \
    X(Y, (_COM_, DatabaseFilter, "database-filter"_tok)) \
    X(Y, (_COM_, DeadInterval, "dead-interval"_tok)) \
    X(Y, (_COM_, DemandCircuit, "demand-circuit"_tok)) \
    X(Y, (_COM_, FloodReduction, "flood-reduction"_tok)) \
    X(Y, (_COM_, HelloInterval, "hello-interval"_tok)) \
    X(Y, (_COM_, MtuIgnore, "mtu-ignore"_tok)) \
    X(Y, (_COM_, Network, "network"_tok)) \
    X(Y, (_COM_, Priority, "priority"_tok)) \
    X(Y, (_COM_, RetransmissionInterval, "retransmission-interval"_tok)) \
    X(Y, (_COM_, TransmitDelay, "transmit-delay"_tok))

/**
 * @brief Shared base parser for common OSPF interface commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Used as a component by both `InterfaceIPOspfCommands` (OSPFv2) and
 * `InterfaceIPv6OspfCommands` / `InterfaceOspfv3Commands` (OSPFv3).
 * Covers `CliMode::Interface` with `InterfaceContext`.
 */
DEFINE_CMD_MODE(InterfaceOspf, CliMode::Interface, config::OspfInterfaceBaseRegistry, INTERFACE_OSPF_LIST);
}

#undef INTERFACE_OSPF_LIST
#undef OSPF_PARAMS

#endif
