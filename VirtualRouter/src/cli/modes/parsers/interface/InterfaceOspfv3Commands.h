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
#include "interface/configs/InterfaceType.hpp"
#include "configs/registry/interface/InterfaceRegistry.h"

#define OSPF_PARAMS DEFINE_PARAMS(config::OspfInterfaceBaseRegistry)
#define INTERFACE_SUB_PARAMS DEFINE_SUB_PARAMS(config::InterfaceRegistry)

namespace cli
{
bool InterfaceOspfv3_Area_Handler(OSPF_PARAMS);
bool InterfaceOspfv3_Neighbor_Handler(OSPF_PARAMS);

#define OSPFV3_LIST(X, Y) \
    X(Y, (INHERIT, InterfaceOspfCommands)) \
    X(Y, (COMMAND, Neighbor, "neighbor"_tok))

DEFINE_CMD_MODE(InterfaceOspfv3, CliMode::Interface, config::OspfInterfaceBaseRegistry, OSPFV3_LIST);

bool InterfaceDefaultOspfv3_Authentication_Handler(OSPF_PARAMS);
bool InterfaceDefaultOspfv3_NullAuthentication_Handler(OSPF_PARAMS);
bool InterfaceDefaultOspfv3_Encryption_Handler(OSPF_PARAMS);
bool InterfaceDefaultOspfv3_NullEncryption_Handler(OSPF_PARAMS);

#define DEFAULT_OSPFV3_LIST(X, Y) \
    X(Y, (INHERIT, InterfaceOspfCommands)) \
    X(Y, (COMMAND, Authentication, "authentication"_tok, "ipsec"_tok)) \
    X(Y, (COMMAND, NullAuthentication, "authentication"_tok, "null"_tok)) \
    X(Y, (COMMAND, Encryption, "encryption"_tok, "ipsec"_tok)) \
    X(Y, (COMMAND, NullEncryption, "encryption"_tok, "null"_tok)) \

DEFINE_CMD_MODE(InterfaceDefaultOspfv3, CliMode::Interface, config::OspfInterfaceBaseRegistry, DEFAULT_OSPFV3_LIST);

bool InterfaceOspfv3Base_ProcessIP_SubHandler(INTERFACE_SUB_PARAMS);
bool InterfaceOspfv3Base_ProcessIPv6_SubHandler(INTERFACE_SUB_PARAMS);
bool InterfaceOspfv3Base_Process_SubHandler(INTERFACE_SUB_PARAMS);
bool InterfaceOspfv3Base_Default_SubHandler(INTERFACE_SUB_PARAMS);

#define INTERFACE_OSPFV3_LIST(X, Y) \
    X(Y, (SUBPRSR, ProcessIP, P_NUMRNG, "ipv4"_tok)) \
    X(Y, (SUBPRSR, ProcessIPv6, P_NUMRNG, "ipv6"_tok)) \
    X(Y, (SUBPRSR, Process, P_NUMRNG)) \
    X(Y, (SUBPRSR, Default))

/**
 * @brief Parser for OSPFv3 commands in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates area, authentication, encryption, and neighbor commands
 * for OSPFv3 interfaces under `CliMode::Interface` with `InterfaceContext`.
 */
DEFINE_CMD_MODE(InterfaceOspfv3Base, CliMode::Interface, config::InterfaceRegistry, INTERFACE_OSPFV3_LIST);
}

#undef INTERFACE_OSPFV3_LIST
#undef OSPF_PARAMS
#undef INTERFACE_SUB_PARAMS

#endif // INTERFACE_IPV6_OSPF_COMMANDS_H

