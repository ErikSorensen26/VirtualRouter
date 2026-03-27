/**
 * @file InterfaceIPv6OspfCommands.h
 * @brief CLI parser for the `ipv6 ospf` sub-tree of Interface Configuration mode.
 *
 * Defines OSPFv3 (IPv6) interface-level commands reachable via `ipv6 ospf <...>`
 * in `CliMode::Interface`.  Extends the shared `InterfaceOspfCommands` base with
 * IPv6-specific additions: process-area association, authentication, encryption,
 * and static neighbor configuration.
 */

#ifndef INTERFACE_IPV6_OSPF_COMMANDS_H
#define INTERFACE_IPV6_OSPF_COMMANDS_H

#include "InterfaceOspfCommands.h"

namespace cli
{
bool InterfaceIPv6Ospf_Area_Handler(INTERFACE_PARAMS);
using InterfaceIPv6Ospf_Area = commandAdder<InterfaceContext,
    InterfaceIPv6Ospf_Area_Handler,
    ARG, "area"_tok, ARG, ARG_REST
>;

bool InterfaceIPv6Ospf_Authentication_Handler(INTERFACE_PARAMS);
using InterfaceIPv6Ospf_Authentication = commandAdder<InterfaceContext,
    InterfaceIPv6Ospf_Authentication_Handler,
    "authentication"_tok, ARG_REST
>;

bool InterfaceIPv6Ospf_Encryption_Handler(INTERFACE_PARAMS);
using InterfaceIPv6Ospf_Encryption = commandAdder<InterfaceContext,
    InterfaceIPv6Ospf_Encryption_Handler,
    "encryption"_tok, ARG_REST
>;

bool InterfaceIPv6Ospf_Neighbor_Handler(INTERFACE_PARAMS);
using InterfaceIPv6Ospf_Neighbor = commandAdder<InterfaceContext,
    InterfaceIPv6Ospf_Neighbor_Handler,
    "neighbor"_tok, ARG_REST
>;

/**
 * @brief Parser for the `ipv6 ospf` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Extends `InterfaceOspfCommands` (shared OSPFv2/v3 base) with OSPFv3-specific
 * interface commands.  Covers `CliMode::Interface` with `InterfaceContext`.
 */
using InterfaceIPv6OspfCommands = CliModeParser<CliMode::Interface, InterfaceContext,
    InterfaceOspfCommands,
    InterfaceIPv6Ospf_Area,
    InterfaceIPv6Ospf_Authentication,
    InterfaceIPv6Ospf_Encryption,
    InterfaceIPv6Ospf_Neighbor
>;
}

#endif // INTERFACE_IPV6_OSPF_COMMANDS_H
