// PrivilegedExecCommands.cpp    

#include <Global.h>

#include "PrivilegedExecCommands.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliEngine.h"

#define PRIVILEGED_EXEC_PARAMS DEFINE_PARAMS(config::GlobalRegistry)

namespace cli
{
bool PrivilegedExec_ConfigureTerm_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(segs);
    ctx.terminal.changeMode<CliMode::GlobalConfiguration>(ctx.configs);
    ctx.terminal.controller.print("\r\nEnter configuration commands, one per line. End with CNTL/Z.");
    return true;
}

bool PrivilegedExec_Exit_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(segs);
    if (!ctx.terminal.popMode())
        ctx.terminal.resetAndChangeMode<CliMode::UserExec>(ctx.terminal.engine.global.configs);
    return true;
}

bool PrivilegedExec_ShowHistory_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(segs);
    for (std::string str : ctx.terminal.getHistory())
    {
        if (str != "")
        {
            ctx.terminal.controller.print("\r\n " + str);
        }
    }
    return true;
}

bool PrivilegedExec_ShowClock_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(segs);
    ctx.terminal.controller.print("\r\n" + ctx.terminal.engine.timeKeeper.getTime());
    return true;
}

bool PrivilegedExec_WriteMem_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(segs);
    //ctx.terminal.engine.saveConfig();
    return false;
}

bool PrivilegedExec_TerminalWidth_Handler(PRIVILEGED_EXEC_PARAMS)
{
/*
    if (args.empty() || !ctx.terminal.engine.isNumeric(args[0])) return false;
    long w = std::stol(args[0]);
    if (w < 40 || w > 512)
    {
        ctx.terminal.controller.print("\r\n% Width must be between 40 and 512");
        return false;
    }
    ctx.terminal.setTerminalWidth(static_cast<size_t>(w));
*/
    return false;
}
}
