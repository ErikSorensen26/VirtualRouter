// PrivilegedExecCommands.cpp    

#include <Global.h>

#include "PrivilegedExecCommands.h"
#include "cli/modes/contexts/GlobalContext.hpp"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliEngine.h"

namespace Cli
{
bool PrivilegedExec_ConfigureTerm_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.changeMode<CliMode::GlobalConfiguration>(ctx.terminal.engine.global, *ctx.terminal.engine.global.getRoutingInstance("default"));
    ctx.terminal.iConsole->print("\r\nEnter configuration commands, one per line. End with CNTL/Z.");
    return true;
}

bool PrivilegedExec_Exit_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.exitMode<CliMode::UserExec>();
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
    ctx.terminal.iConsole->print("\r\n" + ctx.terminal.engine.timeKeeper.getTime());
    return true;
}

bool PrivilegedExec_WriteMem_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.engine.saveConfig();
    return true;
}
}
