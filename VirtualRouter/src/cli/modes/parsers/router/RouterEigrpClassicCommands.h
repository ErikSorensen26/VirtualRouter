/**
 * @file RouterEigrpClassicCommands.h
 * @brief CLI parser for EIGRP classic mode commands.
 *
 * Defines commands for EIGRP classic (flat) configuration model,
 * including network/neighbor setup, address-family entry point,
 * metrics, logging, and topology access.
 */

#ifndef ROUTER_EIGRP_CLASSIC_COMMANDS_H
#define ROUTER_EIGRP_CLASSIC_COMMANDS_H

#include "cli/modes/parsers/router/RouterEigrpCommands.h"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpRegistry)

namespace cli
{
//bool RouterEigrpClassic_DefaultInformation_Handler(EIGRP_PARAMS) {} //TODO
bool RouterEigrpClassic_Exit_Handler(EIGRP_PARAMS);
bool RouterEigrpClassic_PassiveInterface_Handler(EIGRP_PARAMS);

#define ROUTER_EIGRP_CLASSIC_LIST(X, Y) \
    X(Y, (_COM_, Exit, "exit"_tok)) \
    X(Y, (_COM_, PassiveInterface, "passive-interface"_tok))

/**
 * @brief Parser for EIGRP classic mode commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates network/neighbor configuration, logging, metrics, stub mode,
 * and topology base access for classic (flat) EIGRP model.
 */
DEFINE_CMD_MODE(RouterEigrpClassic, CliMode::None, config::EigrpRegistry, ROUTER_EIGRP_CLASSIC_LIST)

#define ROUTER_EIGRP_CLASSIC_LIST_V4(X, Y) \
    X(Y, (_EXT_, RouterEigrpCommands)) \
    X(Y, (_EXT_, RouterEigrpClassicCommands))

/**
 * @brief IPv4 classic mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpClassicV4, CliMode::RouterEigrpClassicV4, config::EigrpRegistry, ROUTER_EIGRP_CLASSIC_LIST_V4)

#define ROUTER_EIGRP_CLASSIC_VRF_LIST(X, Y) \
    X(Y, (_EXT_, RouterEigrpCommands)) \
    X(Y, (_EXT_, RouterEigrpClassicCommands))

/**
 * @brief IPv4 classic vrf mode parser
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpClassicVrf, CliMode::RouterEigrpClassicVRF, config::EigrpRegistry, ROUTER_EIGRP_CLASSIC_VRF_LIST)

#define ROUTER_EIGRP_CLASSIC_LIST_V6(X, Y) \
    X(Y, (_EXT_, RouterEigrpCommands)) \
    X(Y, (_EXT_, RouterEigrpClassicCommands))

/**
 * @brief IPv6 classic mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpClassicV6, CliMode::RouterEigrpClassicV6, config::EigrpRegistry, ROUTER_EIGRP_CLASSIC_LIST_V6);
}

#undef ROUTER_EIGRP_CLASSIC_LIST
#undef ROUTER_EIGRP_CLASSIC_LIST_V4
#undef ROUTER_EIGRP_CLASSIC_LIST_V6
#undef EIGRP_PARAMS

#endif // ROUTER_EIGRP_COMMANDS_H
