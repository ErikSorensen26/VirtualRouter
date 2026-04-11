/**
 * @file InterfaceCommands.h
 * @brief Top-level CLI mode parser for the Interface Configuration mode.
 *
 * Aggregates all commands available in `CliMode::Interface`, including
 * exit, shutdown, and sub-trees for `ip`, `ipv6`, and `ospfv3`.
 */

#ifndef INTERFACE_COMMANDS_H
#define INTERFACE_COMMANDS_H

#include "configs/registry/interface/InterfaceRegistry.h"
#include "interface/configs/InterfaceType.hpp"
#include "cli/parser/CliModeParser.hpp"

#define INTERFACE_PARAMS DEFINE_PARAMS(config::InterfaceRegistry)
#define INTERFACE_SUB_PARAMS DEFINE_SUB_PARAMS(config::InterfaceRegistry)

namespace cli
{
bool Interface_Exit_Handler(INTERFACE_PARAMS);
bool Interface_Shutdown_Handler(INTERFACE_PARAMS);

bool Interface_IP_SubHandler(INTERFACE_SUB_PARAMS);
bool Interface_IPv6_SubHandler(INTERFACE_SUB_PARAMS);
bool Interface_Ospfv3_SubHandler(INTERFACE_SUB_PARAMS);

#define INTERFACE_LIST(X, Y) \
    X(Y, (COMMAND, Exit, "exit"_tok)) \
    X(Y, (SUBPRSR, IP, "ip"_tok)) \
    X(Y, (SUBPRSR, IPv6, "ipv6"_tok)) \
    X(Y, (SUBPRSR, Ospfv3, "ospfv3"_tok)) \
    X(Y, (COMMAND, Shutdown, "shutdown"_tok))


/**
 * @brief Complete parser for the Interface Configuration CLI mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::Interface` with `InterfaceContext` and composes
 * exit, shutdown, and `ip`/`ipv6` sub-trees.
 */
DEFINE_CMD_MODE(Interface, CliMode::Interface, config::InterfaceRegistry, INTERFACE_LIST);
}

#undef INTERFACE_LIST
#undef INTERFACE_PARAMS
#undef INTERFACE_SUB_PARAMS

#endif // INTERFACE_COMMANDS_H
