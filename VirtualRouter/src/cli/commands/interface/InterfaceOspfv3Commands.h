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

#include "configs/registry/interface/InterfaceRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(InterfaceOspfv3, CliMode::Interface, config::OspfGlobalInterfaceRegistry);
DEFINE_CMD_EXECUTOR(InterfaceDefaultOspfv3, CliMode::Interface, config::OspfGlobalInterfaceRegistry);
DEFINE_CMD_EXECUTOR(InterfaceOspfv3Base, CliMode::Interface, config::InterfaceRegistry);
}

#endif // INTERFACE_IPV6_OSPF_COMMANDS_H
