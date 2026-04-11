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
#include "configs/registry/router/OspfInterfaceRegistry.h"

#define OSPF_PARAMS DEFINE_PARAMS(config::OspfInterfaceBaseRegistry)

namespace cli
{
bool InterfaceIPv6Ospf_Area_Handler(OSPF_PARAMS);
bool InterfaceIPv6Ospf_Authentication_Handler(OSPF_PARAMS);
bool InterfaceIPv6Ospf_Encryption_Handler(OSPF_PARAMS);
bool InterfaceIPv6Ospf_Neighbor_Handler(OSPF_PARAMS);

#define INTERFACE_IPV6_OSPF_LIST(X, Y) \
    X(Y, (INHERIT, InterfaceOspfCommands)) \
    X(Y, (COMMAND, Area, P_ARG, "area"_tok)) \
    X(Y, (COMMAND, Authentication, "authentication"_tok)) \
    X(Y, (COMMAND, Encryption, "encryption"_tok)) \
    X(Y, (COMMAND, Neighbor, "neighbor"_tok))

/**
 * @brief Parser for the `ipv6 ospf` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Extends `InterfaceOspfCommands` (shared OSPFv2/v3 base) with OSPFv3-specific
 * interface commands.  Covers `CliMode::Interface` with `InterfaceContext`.
 */
DEFINE_CMD_MODE(InterfaceIPv6Ospf, CliMode::Interface, config::OspfInterfaceBaseRegistry, INTERFACE_IPV6_OSPF_LIST);
}

#undef INTERFACE_IPV6_OSPF_LIST
#undef OSPF_PARAMS

#endif // INTERFACE_IPV6_OSPF_COMMANDS_H
