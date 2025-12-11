// PrivilegedExecCommands.cpp    

#include "PrivilegedExecCommands.h"
#include <GlobalContext.hpp>
#include <UserExecContext.hpp>
#include <Global.h>
#include <CliSession.h>
#include <CliEngine.h>

namespace Cli
{
bool PrivilegedExec_ConfigureTerm_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.changeMode(CliMode::GlobalConfiguration);
    ctx.terminal.iConsole->print("\r\nEnter configuration commands, one per line. End with CNTL/Z.");
    ctx.terminal.changeModeConfig(new GlobalContext(
        ctx, ctx.terminal.engine.global,
        *ctx.terminal.engine.global.getRoutingInstance("default"))
    );
    return true;
}

bool PrivilegedExec_Exit_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.exitMode(CliMode::UserExec);
    ctx.terminal.changeModeConfig(new UserExecContext(ctx));
    return true;
}

bool PrivilegedExec_ShowHistory_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    for (std::string str : ctx.terminal.getHistory())
    {
        if (str != "")
        {
            ctx.terminal.iConsole->print("\r\n " + str);
        }
    }
    return true;
}

bool PrivilegedExec_ShowClock_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.iConsole->print("\r\n" + ctx.terminal.engine.timeManager.getTime());
    return true;
}

bool PrivilegedExec_WriteMem_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.engine.saveConfig();
    return true;
}
}
