/**
 * @file RouterEigrpCommands.h
 * @brief CLI parser for EIGRP commands.
 *
 * Defines commands for EIGRP classic (flat) configuration model,
 * including network/neighbor setup, address-family entry point,
 * metrics, logging, and topology access.
 */

#ifndef ROUTER_EIGRP_COMMANDS_H
#define ROUTER_EIGRP_COMMANDS_H

#include "configs/registry/router/EigrpRegistry.h"
#include "cli/parser/CliModeParser.hpp"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpRegistry)

namespace cli
{
//bool RouterEigrpClassic_DefaultInformation_Handler(EIGRP_PARAMS) {} //TODO
bool RouterEigrp_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS);
bool RouterEigrp_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS);
bool RouterEigrp_EigrpRouterId_Handler(EIGRP_PARAMS);
bool RouterEigrp_EigrpStub_Handler(EIGRP_PARAMS);
bool RouterEigrp_MetricWeights_Handler(EIGRP_PARAMS);
bool RouterEigrp_Neighbor_Handler(EIGRP_PARAMS);
bool RouterEigrp_Network_Handler(EIGRP_PARAMS);
bool RouterEigrp_TimersGracefulRestart_Handler(EIGRP_PARAMS);

#define ROUTER_EIGRP_LIST(X, Y) \
    X(Y, (COMMAND, EigrpLogNeighborChanges, "eigrp"_tok, "log-neighbor-changes"_tok)) \
    X(Y, (COMMAND, EigrpLogNeighborWarnings, "eigrp"_tok, "log-neighbor-warnings"_tok)) \
    X(Y, (COMMAND, EigrpRouterId, "eigrp"_tok, "router-id"_tok)) \
    X(Y, (COMMAND, EigrpStub, "eigrp"_tok, "stub"_tok)) \
    X(Y, (COMMAND, MetricWeights, "metric"_tok, "weights"_tok)) \
    X(Y, (COMMAND, Neighbor, "neighbor"_tok)) \
    X(Y, (COMMAND, Network, "network"_tok)) \
    X(Y, (COMMAND, TimersGracefulRestart, "timers"_tok, "graceful-restart"_tok))

/**
 * @brief Parser for EIGRP classic mode commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates network/neighbor configuration, logging, metrics, stub mode,
 * and topology base access for classic (flat) EIGRP model.
 */
DEFINE_CMD_MODE(RouterEigrp, CliMode::None, config::EigrpRegistry, ROUTER_EIGRP_LIST)
}

#undef ROUTER_EIGRP_LIST
#undef EIGRP_PARAMS

#endif // ROUTER_EIGRP_COMMANDS_H
