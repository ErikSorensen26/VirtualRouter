// UserExecCommands

#include "UserExecCommands.h"
#include "cli/runtime/CliSession.h"

namespace Cli
{
bool UserExec_Enable_Handler(USER_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.changeMode<CliMode::PrivilegedExec>();
    return true;
}

bool UserExec_Exit_Handler(USER_EXEC_PARAMS)
{
    UNUSED(ctx);
    UNUSED(args);
    exit(1);
    return true;
}
}
