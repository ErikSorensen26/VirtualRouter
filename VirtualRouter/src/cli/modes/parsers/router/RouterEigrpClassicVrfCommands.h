/**
 * @file RouterEigrpClassicVrfCommands.h
 * @brief CLI parser for EIGRP classic commands in VRF context.
 *
 * Defines EIGRP classic commands available within a VRF namespace,
 * including network declarations and topology access.
 */

#ifndef ROUTER_EIGRP_CLASSIC_VRF_COMMANDS_H
#define ROUTER_EIGRP_CLASSIC_VRF_COMMANDS_H

#include "RouterEigrpTopologyCommands.h"

namespace cli
{
//bool RouterEigrpClassicVrf_DefaultInformation_Handler(EIGRP_PARAMS) {} //TODO

bool RouterEigrpClassicVrf_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_EigrpLogNeighborChanges = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_EigrpLogNeighborChanges_Handler,
    "eigrp"_tok, "log-neighbor-changes"_tok
>;

bool RouterEigrpClassicVrf_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_EigrpLogNeighborWarnings = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_EigrpLogNeighborWarnings_Handler,
    "eigrp"_tok, "log-neighbor-warnings"_tok, ARG_REST
>;

bool RouterEigrpClassicVrf_EigrpRouterId_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_EigrpRouterId = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_EigrpRouterId_Handler,
    "eigrp"_tok, "router-id"_tok, ARG_REST
>;

bool RouterEigrpClassicVrf_EigrpStub_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_EigrpStub = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_EigrpStub_Handler,
    "eigrp"_tok, "stub"_tok, ARG_REST
>;

bool RouterEigrpClassicVrf_Exit_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_Exit = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_Exit_Handler,
    "exit-address-family"_tok
>;

bool RouterEigrpClassicVrf_MetricWeights_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_MetricWeights = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_MetricWeights_Handler,
    "metric"_tok, "weights"_tok, ARG_REST
>;

bool RouterEigrpClassicVrf_Neighbor_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_Neighbor = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_MetricWeights_Handler,
    "neighbor"_tok, ARG, ARG_REST
>;

bool RouterEigrpClassicVrf_Network_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_Network = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_Network_Handler,
    "network"_tok, ARG, ARG_REST
>;

bool RouterEigrpClassicVrf_PassiveInterface_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_PassiveInterface = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_PassiveInterface_Handler,
    "passive-interface"_tok, ARG, ARG
>;

bool RouterEigrpClassicVrf_TimersGracefulRestart_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_TimersGracefulRestart = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_TimersGracefulRestart_Handler,
    "timers"_tok, "graceful-restart"_tok, ARG_REST
>;

/**
 * @brief Parser for EIGRP classic commands within a VRF namespace.
 * @ingroup CLI_MODE_PARSERS
 *
 * Provides VRF-specific EIGRP classic configuration including networks,
 * neighbors, metrics, and topology access.
 */
using RouterEigrpClassicVrfCommands = CliModeParser<CliMode::RouterEigrpClassicVRF, EigrpContext,
    RouterEigrpTopologyCommands,
    RouterEigrpClassicVrf_EigrpLogNeighborChanges,
    RouterEigrpClassicVrf_EigrpLogNeighborWarnings,
    RouterEigrpClassicVrf_EigrpRouterId,
    RouterEigrpClassicVrf_EigrpStub,
    RouterEigrpClassicVrf_Exit,
    RouterEigrpClassicVrf_MetricWeights,
    RouterEigrpClassicVrf_Neighbor,
    RouterEigrpClassicVrf_Network,
    RouterEigrpClassicVrf_PassiveInterface,
    RouterEigrpClassicVrf_TimersGracefulRestart
>;
}

#endif // ROUTER_EIGRP_CLASSIC_VRF_COMMANDS_H
