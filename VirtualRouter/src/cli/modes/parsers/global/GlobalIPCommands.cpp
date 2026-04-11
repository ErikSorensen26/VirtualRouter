// GlobalIPCommands.cpp

#include "GlobalIPCommands.h"
#include "GlobalIPDHCPCommands.h"

#define GLOBAL_SUB_PARAMS DEFINE_SUB_PARAMS(config::GlobalRegistry)

namespace cli
{
bool GlobalIP_DHCP_SubHandler(GLOBAL_SUB_PARAMS)
{
    return GlobalIPDHCPCommands::execute(ctx, toks, idx);
}
}
