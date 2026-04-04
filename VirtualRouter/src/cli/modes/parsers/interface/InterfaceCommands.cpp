// InterfaceCommands.cpp

#include "InterfaceCommands.h"

#include "cli/runtime/CliSession.h"
#include "interface/Interface.h"
#include "cli/modes/Mode.hpp"
#include "cli/runtime/CliEngine.h"

namespace cli
{
bool Interface_Exit_Handler(INTERFACE_PARAMS)
{
    UNUSED(segs);
    ctx.terminal.exitMode<CliMode::GlobalConfiguration>(ctx.terminal.engine.global, *ctx.currentInterface.getVRF());
    return true;
}

bool Interface_Shutdown_Handler(INTERFACE_PARAMS)
{
    UNUSED(segs);
    ctx.currentInterface.shutdown(!ctx.negate && !ctx.defaulted);
    return true;
}
}
