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

#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli::execution
{
DEFINE_CMD_EXECUTOR(InterfaceOspf, CliMode::Interface, config::OspfGlobalInterfaceRegistry);
}

#endif
