// GlobalIPCommands.h

#ifndef GLOBAL_IP_COMMANDS_H
#define GLOBAL_IP_COMMANDS_H

#include <CliModeParser.hpp>
#include <GlobalContext.hpp>
#include "GlobalIPDHCPCommands.h"

namespace Cli
{
bool GlobalIP_DHCP_Handler(GLOBAL_PARAMS);
using GlobalIP_DHCP = subAdder<GlobalContext,
    GlobalIPDHCPCommands,
    "dhcp"_tok
>;

using GlobalIPCommands = CliModeParser<CliMode::GlobalConfiguration, GlobalContext,
    GlobalIP_DHCP
>;
}

#endif
