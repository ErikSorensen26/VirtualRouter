// InterfaceOspfv3Commands.h

#ifndef INTERFACE_OSPFV3_COMMANDS_H
#define INTERFACE_OSPFV3_COMMANDS_H

#include "InterfaceOspfCommands.h"

namespace cli
{
bool InterfaceOspfv3_Area_Handler(INTERFACE_PARAMS);
using InterfaceOspfv3_Area = commandAdder<InterfaceContext,
    InterfaceOspfv3_Area_Handler,
    ARG, "area"_tok, ARG, ARG_REST
>;

bool InterfaceOspfv3_Authentication_Handler(INTERFACE_PARAMS);
using InterfaceOspfv3_Authentication = commandAdder<InterfaceContext,
    InterfaceOspfv3_Authentication_Handler,
    "authentication"_tok, ARG_REST
>;

bool InterfaceOspfv3_Encryption_Handler(INTERFACE_PARAMS);
using InterfaceOspfv3_Encryption = commandAdder<InterfaceContext,
    InterfaceOspfv3_Encryption_Handler,
    "encryption"_tok, ARG_REST
>;

bool InterfaceOspfv3_Neighbor_Handler(INTERFACE_PARAMS);
using InterfaceOspfv3_Neighbor = commandAdder<InterfaceContext,
    InterfaceOspfv3_Neighbor_Handler,
    "neighbor"_tok, ARG_REST
>;

using InterfaceOspfv3Commands = CliModeParser<CliMode::Interface, InterfaceContext,
    InterfaceOspfCommands,
    InterfaceOspfv3_Area,
    InterfaceOspfv3_Authentication,
    InterfaceOspfv3_Encryption,
    InterfaceOspfv3_Neighbor
>;
}

#endif // INTERFACE_IPV6_OSPF_COMMANDS_H

