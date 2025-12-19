// GlobalIPCommands.cpp

#include "GlobalIPCommands.h"
#include "GlobalIPDHCPCommands.h"

namespace Cli
{
bool GlobalIP_DHCP_Handler(GLOBAL_PARAMS)
{
    return GlobalIPDHCPCommands::execute(ctx, args);
}
}
