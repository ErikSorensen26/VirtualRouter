// GlobalIPCommands.cpp

#include "GlobalIPCommands.h"
#include "cli/parser/CliModeParser.hpp"
#include "GlobalIPDHCPCommands.h"

#define GLOBAL_SUB_PARAMS DEFINE_SUB_PARAMS(config::GlobalRegistry)

namespace cli
{
bool GlobalIP_DHCP_SubHandler(GLOBAL_SUB_PARAMS)
{
    return GlobalIPDHCPCommands::execute(ctx, toks, idx);
}

#define GLOBAL_IP_LIST(X, Y) \
    X(Y, (SUBPRSR, DHCP, "dhcp"_tok))

DEFINE_CMD_MODE(GlobalIP, config::GlobalRegistry, GLOBAL_IP_LIST);
}
