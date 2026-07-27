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

#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(InterfaceIPv6Ospf, CliMode::Interface, config::OspfGlobalInterfaceRegistry);
}

#endif // INTERFACE_IPV6_OSPF_COMMANDS_H
