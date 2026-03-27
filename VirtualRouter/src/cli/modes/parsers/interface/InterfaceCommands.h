/**
 * @file InterfaceCommands.h
 * @brief Top-level CLI mode parser for the Interface Configuration mode.
 *
 * Aggregates all commands available in `CliMode::Interface`, including
 * exit, shutdown, and sub-trees for `ip`, `ipv6`, and `ospfv3`.
 */

#ifndef INTERFACE_COMMANDS_H
#define INTERFACE_COMMANDS_H

#include "InterfaceIPCommands.h"
#include "InterfaceIPv6Commands.h"
#include "InterfaceOspfv3Commands.h"

namespace cli
{
bool Interface_Exit_Handler(INTERFACE_PARAMS);
using Interface_Exit = commandAdder<InterfaceContext,
    Interface_Exit_Handler,
    "exit"_tok
>;

using Interface_IP = subAdder<InterfaceContext,
    InterfaceIPCommands,
    "ip"_tok
>;

using Interface_IPv6 = subAdder<InterfaceContext,
    InterfaceIPv6Commands,
    "ipv6"_tok
>;

using Interface_Ospfv3 = subAdder<InterfaceContext,
    InterfaceOspfv3Commands,
    "ospfv3"_tok
>;

bool Interface_Shutdown_Handler(INTERFACE_PARAMS);
using Interface_Shutdown = commandAdder<InterfaceContext,
    Interface_Shutdown_Handler,
    "shutdown"_tok
>;

/**
 * @brief Complete parser for the Interface Configuration CLI mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::Interface` with `InterfaceContext` and composes
 * exit, shutdown, and `ip`/`ipv6` sub-trees.
 */
using InterfaceCommands = CliModeParser<CliMode::Interface, InterfaceContext,
    Interface_Exit,
    Interface_IP,
    Interface_IPv6,
    Interface_Shutdown
>;
}

#endif // INTERFACE_COMMANDS_H
