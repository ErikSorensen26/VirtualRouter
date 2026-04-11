// RouterEigrpCommands.cpp

#include <Global.h>
#include <VirtualRouter.h>

#include "RouterEigrpClassicCommands.h"
#include "cli/parser/CommandUtils.hpp"
#include "configs/registry/router/EigrpRegistry.h"
#include "cli/runtime/CliSession.h"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpRegistry)

namespace cli
{
bool RouterEigrpClassic_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.exitMode<CliMode::GlobalConfiguration, config::GlobalRegistry>(ctx.configs);
}

bool RouterEigrpClassic_PassiveInterface_Handler(EIGRP_PARAMS)
{
    auto& passive = ctx.configs.get<config::Eigrp::PASSIVE_INTERFACES>();
    config::DefType<decltype(passive)>::node tup;
    if (!utils::setDoubleValue(tup, segs[0] >> 0, segs[0] >> 1))
        return false;
    return utils::setListEntry(passive, ctx, tup);
}
}

#undef EIGRP_PARAMS
