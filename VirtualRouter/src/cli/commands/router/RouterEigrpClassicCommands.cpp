// RouterEigrpCommands.cpp

#include "RouterEigrpClassicCommands.h"
#include "cli/grammar/CliModeParser.hpp"

#include "RouterEigrpCommands.h"
#include "cli/grammar/CommandUtils.hpp"
#include "configs/registry/router/EigrpRegistry.h"
#include "cli/session/CliSession.h"
#include "configs/FieldAccessor.hpp"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpRegistry)

namespace cli
{
bool RouterEigrpClassic_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.popMode();
}

bool RouterEigrpClassic_PassiveInterface_Handler(EIGRP_PARAMS)
{
    auto passive = ctx.configs().get<config::Eigrp::PASSIVE_INTERFACES>();
    config::DefType<typename decltype(passive)::Field>::node tup;
    if (!utils::setDoubleValue(tup, segs[0] >> 0, segs[0] >> 1))
        return false;
    return utils::setListEntry(passive, ctx, tup);
}

//bool RouterEigrpClassic_DefaultInformation_Handler(EIGRP_PARAMS) {} //TODO

#define ROUTER_EIGRP_CLASSIC_LIST(X, Y) \
    X(Y, (COMMAND, Exit, "exit"_tok)) \
    X(Y, (COMMAND, PassiveInterface, "passive-interface"_tok))

/**
 * @brief Parser for EIGRP classic mode commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates network/neighbor configuration, logging, metrics, stub mode,
 * and topology base access for classic (flat) EIGRP model.
 */
DEFINE_CMD_MODE(RouterEigrpClassic, config::EigrpRegistry, ROUTER_EIGRP_CLASSIC_LIST)

#define ROUTER_EIGRP_CLASSIC_LIST_V4(X, Y) \
    X(Y, (CMD_INHERIT, RouterEigrpCommands)) \
    X(Y, (CMD_INHERIT, RouterEigrpClassicCommands))

/**
 * @brief IPv4 classic mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpClassicV4, config::EigrpRegistry, ROUTER_EIGRP_CLASSIC_LIST_V4)

#define ROUTER_EIGRP_CLASSIC_VRF_LIST(X, Y) \
    X(Y, (CMD_INHERIT, RouterEigrpCommands)) \
    X(Y, (CMD_INHERIT, RouterEigrpClassicCommands))

/**
 * @brief IPv4 classic vrf mode parser
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpClassicVrf, config::EigrpRegistry, ROUTER_EIGRP_CLASSIC_VRF_LIST)

#define ROUTER_EIGRP_CLASSIC_LIST_V6(X, Y) \
    X(Y, (CMD_INHERIT, RouterEigrpCommands)) \
    X(Y, (CMD_INHERIT, RouterEigrpClassicCommands))

/**
 * @brief IPv6 classic mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpClassicV6, config::EigrpRegistry, ROUTER_EIGRP_CLASSIC_LIST_V6);
}

#undef EIGRP_PARAMS
