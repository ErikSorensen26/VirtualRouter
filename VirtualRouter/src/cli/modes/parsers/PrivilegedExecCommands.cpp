// PrivilegedExecCommands.cpp    

#include <Global.h>

#include "PrivilegedExecCommands.h"
#include "cli/modes/contexts/GlobalContext.hpp"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliEngine.h"

namespace cli
{
bool PrivilegedExec_ConfigureTerm_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.changeMode<CliMode::GlobalConfiguration>(ctx.terminal.engine.global, *ctx.terminal.engine.global.getRoutingInstance("default"));
    ctx.terminal.controller.print("\r\nEnter configuration commands, one per line. End with CNTL/Z.");
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
            ctx.terminal.controller.print("\r\n " + str);
        }
    }
    return true;
}

bool PrivilegedExec_ShowClock_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.controller.print("\r\n" + ctx.terminal.engine.timeKeeper.getTime());
    return true;
}

bool PrivilegedExec_WriteMem_Handler(PRIVILEGED_EXEC_PARAMS)
{
    UNUSED(args);
    ctx.terminal.engine.saveConfig();
    return true;
}

bool PrivilegedExec_TerminalWidth_Handler(PRIVILEGED_EXEC_PARAMS)
{
    if (args.empty() || !ctx.terminal.engine.isNumeric(args[0])) return false;
    long w = std::stol(args[0]);
    if (w < 40 || w > 512)
    {
        ctx.terminal.controller.print("\r\n% Width must be between 40 and 512");
        return false;
    }
    ctx.terminal.setTerminalWidth(static_cast<size_t>(w));
    return true;
}
}
