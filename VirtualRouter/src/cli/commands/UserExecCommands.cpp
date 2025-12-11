// UserExecCommands

#include <UserExecCommands.h>
#include <PrivilegedExecContext.hpp>
#include <CliSession.h>

namespace Cli
{
bool UserExec_Enable_Handler(USER_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.changeMode(CliMode::PrivilegedExec);
    ctx.terminal.changeModeConfig(new PrivilegedExecContext(ctx));
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
