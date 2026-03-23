// InterfaceIPv6OspfCommands.h

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

using InterfaceIPv6OspfCommands = CliModeParser<CliMode::Interface, InterfaceContext,
    InterfaceOspfCommands,
    InterfaceIPv6Ospf_Area,
    InterfaceIPv6Ospf_Authentication,
    InterfaceIPv6Ospf_Encryption,
    InterfaceIPv6Ospf_Neighbor
>;
}

#endif // INTERFACE_IPV6_OSPF_COMMANDS_H

