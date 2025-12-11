// GlobalIPCommands.h

#ifndef GLOBAL_IP_COMMANDS_H
#define GLOBAL_IP_COMMANDS_H

#include <CliModeParser.hpp>
#include <GlobalContext.hpp>

namespace Cli
{
bool GlobalIP_DHCP_Handler(GLOBAL_PARAMS);
using GlobalIP_DHCP = commandAdder<GlobalContext,
    GlobalIP_DHCP_Handler,
    "dhcp"_tok, ARG_REST
>;

using GlobalIPCommands = CliModeParser<GlobalContext,
    GlobalIP_DHCP
>;
}

#endif
