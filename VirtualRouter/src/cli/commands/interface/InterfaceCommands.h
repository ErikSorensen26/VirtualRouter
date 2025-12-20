// InterfaceCommands.h

#ifndef INTERFACE_COMMANDS_H
#define INTERFACE_COMMANDS_H

#include <CliModeParser.hpp>
#include <InterfaceContext.hpp>

namespace Cli
{
bool Interface_Exit_Handler(INTERFACE_PARAMS);
using Interface_Exit = commandAdder<InterfaceContext,
    Interface_Exit_Handler,
    "exit"_tok
>;

bool Interface_IP_Handler(INTERFACE_PARAMS);
using Interface_IP = commandAdder<InterfaceContext,
    Interface_IP_Handler,
    "ip"_tok, ARG_REST
>;

bool Interface_IPv6_Handler(INTERFACE_PARAMS);
using Interface_IPv6 = commandAdder<InterfaceContext,
    "ipv6"_tok, ARG_REST
>;

bool Interface_Shutdown_Handler(INTERFACE_PARAMS);
using Interface_Shutdown = commandAdder<InterfaceContext,
    Interface_Shutdown_Handler,
    "shutdown"_tok
>;
}

#endif // INTERFACE_COMMANDS_H
