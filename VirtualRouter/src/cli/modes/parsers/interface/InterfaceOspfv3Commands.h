/**
 * @file InterfaceOspfv3Commands.h
 * @brief CLI parser for OSPFv3 commands in Interface Configuration mode.
 *
 * Defines OSPFv3-specific commands reachable in Interface Configuration mode,
 * including area assignment, authentication/encryption settings, and neighbor
 * configuration for IPv6 OSPF interfaces.
 */

#ifndef INTERFACE_OSPFV3_COMMANDS_H
#define INTERFACE_OSPFV3_COMMANDS_H

#include "InterfaceOspfCommands.h"

namespace cli
{
bool InterfaceOspfv3_Area_Handler(INTERFACE_PARAMS);
using InterfaceOspfv3_Area = commandAdder<InterfaceContext,
    InterfaceOspfv3_Area_Handler,
    ARG, "area"_tok
>;

bool InterfaceOspfv3_Authentication_Handler(INTERFACE_PARAMS);
using InterfaceOspfv3_Authentication = commandAdder<InterfaceContext,
    InterfaceOspfv3_Authentication_Handler,
    "authentication"_tok
>;

bool InterfaceOspfv3_Encryption_Handler(INTERFACE_PARAMS);
using InterfaceOspfv3_Encryption = commandAdder<InterfaceContext,
    InterfaceOspfv3_Encryption_Handler,
    "encryption"_tok
>;

bool InterfaceOspfv3_Neighbor_Handler(INTERFACE_PARAMS);
using InterfaceOspfv3_Neighbor = commandAdder<InterfaceContext,
    InterfaceOspfv3_Neighbor_Handler,
    "neighbor"_tok
>;

/**
 * @brief Parser for OSPFv3 commands in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates area, authentication, encryption, and neighbor commands
 * for OSPFv3 interfaces under `CliMode::Interface` with `InterfaceContext`.
 */
using InterfaceOspfv3Commands = CliModeParser<CliMode::Interface, InterfaceContext,
    InterfaceOspfCommands,
    InterfaceOspfv3_Area,
    InterfaceOspfv3_Authentication,
    InterfaceOspfv3_Encryption,
    InterfaceOspfv3_Neighbor
>;
}

#endif // INTERFACE_IPV6_OSPF_COMMANDS_H

