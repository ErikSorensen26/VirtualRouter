/**
 * @file InterfaceIPOspfCommands.h
 * @brief CLI parser for the `ip ospf` sub-tree of Interface Configuration mode.
 *
 * Defines OSPFv2 interface-level commands reachable via `ip ospf <...>` in
 * `CliMode::Interface`.  Extends the shared `InterfaceOspfCommands` base with
 * IPv4-specific additions: process-area association, authentication (plain-text
 * and MD5), Link-Local Signalling, prefix suppression, resync timeout, shutdown,
 * and TTL security.
 */

#ifndef INTERFACE_IP_OSPF_COMMANDS_H
#define INTERFACE_IP_OSPF_COMMANDS_H

#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(InterfaceIPOspf, CliMode::Interface, config::OspfInterfaceBaseRegistry);
}

#endif // INTERFACE_IP_OSPF_COMMANDS_H
