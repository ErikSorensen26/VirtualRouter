// InterfaceCommands.cpp

#include "InterfaceCommands.h"
#include "InterfaceIPCommands.h"
#include "InterfaceIPv6Commands.h"

#include <CliSession.h>
#include <Interface.h>
#include <Mode.hpp>

namespace Cli
{
bool Interface_Exit_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
	ctx.terminal.exitMode(CliMode::GlobalConfiguration);
    return true;
}

bool Interface_IP_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    InterfaceIPCommands::execute(ctx, args);
    return true;
}

bool Interface_IPv6_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    InterfaceIPv6Commands::execute(ctx, args);
    return true;
}

bool Interface_Shutdown_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.shutdown(!ctx.negate);
    return true;
}


}
