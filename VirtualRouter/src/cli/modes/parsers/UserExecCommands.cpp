// UserExecCommands

#include "UserExecCommands.h"
#include "cli/runtime/CliSession.h"

namespace cli
{
bool UserExec_Enable_Handler(USER_EXEC_PARAMS)
{
    UNUSED(segs);
    ctx.terminal.changeMode<CliMode::PrivilegedExec>(ctx.configs);
    return true;
}

bool UserExec_Exit_Handler(USER_EXEC_PARAMS)
{
    UNUSED(ctx);
    UNUSED(segs);
    exit(1);
    return true;
}
}
