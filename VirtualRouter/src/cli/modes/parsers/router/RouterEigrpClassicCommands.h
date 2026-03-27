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

#include "cli/modes/parsers/router/RouterEigrpTopologyCommands.h"

namespace cli
{
bool RouterEigrpClassic_AddressFamilyVrf_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_AddressFamilyVrf = commandAdder<EigrpContext,
    RouterEigrpClassic_AddressFamilyVrf_Handler,
    "address-family"_tok, "ipv4"_tok, "vrf"_tok, ARG
>;

//bool RouterEigrp_AddressFamilyVrfUnicast_Handler(EIGRP_PARAMS) {} //TODO

//bool RouterEigrpClassic_DefaultInformation_Handler(EIGRP_PARAMS) {} //TODO

bool RouterEigrpClassic_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_EigrpLogNeighborChanges = commandAdder<EigrpContext,
    RouterEigrpClassic_EigrpLogNeighborChanges_Handler,
    "eigrp"_tok, "log-neighbor-changes"_tok
>;

bool RouterEigrpClassic_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_EigrpLogNeighborWarnings = commandAdder<EigrpContext,
    RouterEigrpClassic_EigrpLogNeighborWarnings_Handler,
    "eigrp"_tok, "log-neighbor-warnings"_tok, ARG_REST
>;

bool RouterEigrpClassic_EigrpRouterId_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_EigrpRouterId = commandAdder<EigrpContext,
    RouterEigrpClassic_EigrpRouterId_Handler,
    "eigrp"_tok, "router-id"_tok, ARG_REST
>;

bool RouterEigrpClassic_EigrpStub_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_EigrpStub = commandAdder<EigrpContext,
    RouterEigrpClassic_EigrpStub_Handler,
    "eigrp"_tok, "stub"_tok, ARG_REST
>;

bool RouterEigrpClassic_Exit_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_Exit = commandAdder<EigrpContext,
    RouterEigrpClassic_Exit_Handler,
    "exit"_tok
>;

bool RouterEigrpClassic_MetricWeights_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_MetricWeights = commandAdder<EigrpContext,
    RouterEigrpClassic_MetricWeights_Handler,
    "metric"_tok, "weights"_tok, ARG_REST
>;

bool RouterEigrpClassic_Neighbor_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_Neighbor = commandAdder<EigrpContext,
    RouterEigrpClassic_MetricWeights_Handler,
    "neighbor"_tok, ARG, ARG_REST
>;

bool RouterEigrpClassic_Network_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_Network = commandAdder<EigrpContext,
    RouterEigrpClassic_Network_Handler,
    "network"_tok, ARG, ARG_REST
>;

bool RouterEigrpClassic_PassiveInterface_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_PassiveInterface = commandAdder<EigrpContext,
    RouterEigrpClassic_PassiveInterface_Handler,
    "passive-interface"_tok, ARG, ARG
>;

bool RouterEigrpClassic_TimersGracefulRestart_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_TimersGracefulRestart = commandAdder<EigrpContext,
    RouterEigrpClassic_TimersGracefulRestart_Handler,
    "timers"_tok, "graceful-restart"_tok, ARG_REST
>;

/**
 * @brief Parser for EIGRP classic mode commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates network/neighbor configuration, logging, metrics, stub mode,
 * and topology base access for classic (flat) EIGRP model.
 */
using RouterEigrpClassicCommands = CliModeParser<CliMode::RouterEigrpClassicV4, EigrpContext,
    RouterEigrpTopologyCommands,
    RouterEigrpClassic_AddressFamilyVrf,
    RouterEigrpClassic_EigrpLogNeighborChanges,
    RouterEigrpClassic_EigrpLogNeighborWarnings,
    RouterEigrpClassic_EigrpRouterId,
    RouterEigrpClassic_EigrpStub,
    RouterEigrpClassic_Exit,
    RouterEigrpClassic_MetricWeights,
    RouterEigrpClassic_Neighbor,
    RouterEigrpClassic_Network,
    RouterEigrpClassic_PassiveInterface,
    RouterEigrpClassic_TimersGracefulRestart
>;

/**
 * @brief IPv4 classic mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
using RouterEigrpClassicCommandsV4 = CliModeParser<CliMode::RouterEigrpClassicV4, EigrpContext,
    RouterEigrpClassicCommands
>;

/**
 * @brief IPv6 classic mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
using RouterEigrpClassicCommandsV6 = CliModeParser<CliMode::RouterEigrpClassicV6, EigrpContext,
    RouterEigrpClassicCommands
>;
}

#endif // ROUTER_EIGRP_COMMANDS_H
