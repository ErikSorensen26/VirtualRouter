// InterfaceCommands.h

#ifndef INTERFACE_COMMANDS_H
#define INTERFACE_COMMANDS_H

#include <CliModeParser.hpp>
#include <InterfaceContext.hpp>
#include "InterfaceIPCommands.h"
#include "InterfaceIPv6Commands.h"

namespace Cli
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

bool Interface_Shutdown_Handler(INTERFACE_PARAMS);
using Interface_Shutdown = commandAdder<InterfaceContext,
    Interface_Shutdown_Handler,
    "shutdown"_tok
>;

using InterfaceCommands = CliModeParser<CliMode::Interface, InterfaceContext,
    Interface_Exit,
    Interface_IP,
    Interface_IPv6,
    Interface_Shutdown
>;
}

#endif // INTERFACE_COMMANDS_H
