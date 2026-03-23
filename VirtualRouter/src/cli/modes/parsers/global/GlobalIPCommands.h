// GlobalIPCommands.h

#ifndef GLOBAL_IP_COMMANDS_H
#define GLOBAL_IP_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/SubCommand.hpp"
#include "cli/modes/contexts/GlobalContext.hpp"
#include "GlobalIPDHCPCommands.h"

namespace cli
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
