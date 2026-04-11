// InterfaceCommands.cpp

#include "InterfaceCommands.h"

#include "cli/runtime/CliSession.h"
#include "cli/modes/Mode.hpp"
#include "cli/runtime/CliEngine.h"
#include "cli/parser/CommandUtils.hpp"

#include "InterfaceIPCommands.h"
#include "InterfaceIPv6Commands.h"
#include "InterfaceOspfv3Commands.h"

#define INTERFACE_PARAMS DEFINE_PARAMS(config::InterfaceRegistry)
#define INTERFACE_SUB_PARAMS DEFINE_SUB_PARAMS(config::InterfaceRegistry)

namespace cli
{
bool Interface_Exit_Handler(INTERFACE_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.exitMode<CliMode::GlobalConfiguration, config::GlobalRegistry>(ctx.configs);
}

bool Interface_Shutdown_Handler(INTERFACE_PARAMS)
{
    UNUSED(segs);
    auto& shut = ctx.configs.get<config::Interface::SHUTDOWN>();
    return utils::setFieldValue(shut, ctx, segs[0] >> 1);
}

bool Interface_IP_SubHandler(INTERFACE_SUB_PARAMS)
{
    return InterfaceIPCommands::execute(ctx, toks, idx);
}

bool Interface_IPv6_SubHandler(INTERFACE_SUB_PARAMS)
{
    return InterfaceIPv6Commands::execute(ctx, toks, idx);
}

bool Interface_Ospfv3_SubHandler(INTERFACE_SUB_PARAMS)
{

}
}
