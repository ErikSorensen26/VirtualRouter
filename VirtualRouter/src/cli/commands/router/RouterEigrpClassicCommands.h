// RouterEigrpCommands.h

#ifndef ROUTER_EIGRP_CLASSIC_COMMANDS_H
#define ROUTER_EIGRP_CLASSIC_COMMANDS_H

#include <CliModeParser.hpp>
#include <EigrpContext.hpp>

namespace Cli
{
bool RouterEigrpClassic_AddressFamilyVrf_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_AddressFamilyVrf = commandAdder<EigrpContext,
    RouterEigrpClassic_AddressFamilyVrf_Handler,
    "address-family"_tok, "ipv4"_tok, "vrf"_tok, ARG
>;

//bool RouterEigrp_AddressFamilyVrfUnicast_Handler(EIGRP_PARAMS) {} //TODO

bool RouterEigrpClassic_AutoSummary_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_AutoSummary = commandAdder<EigrpContext,
    RouterEigrpClassic_AutoSummary_Handler,
    "auto-summary"_tok
>;

//bool RouterEigrpClassic_DefaultInformation_Handler(EIGRP_PARAMS) {} //TODO

bool RouterEigrpClassic_DefaultMetric_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_DefaultMetric = commandAdder<EigrpContext,
    RouterEigrpClassic_DefaultMetric_Handler,
    "default-metric"_tok, ARG_REST
>;

bool RouterEigrpClassic_Distance_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_Distance = commandAdder<EigrpContext,
    RouterEigrpClassic_Distance_Handler,
    "distance"_tok, ARG_REST
>;

bool RouterEigrpClassic_EigrpEventLogSize_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_EigrpEventLogSize = commandAdder<EigrpContext,
    RouterEigrpClassic_EigrpEventLogSize_Handler,
    "eigrp"_tok, "event-log-size"_tok, ARG_REST
>;

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

bool RouterEigrpClassic_MaximumPaths_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_MaximumPaths = commandAdder<EigrpContext,
    RouterEigrpClassic_MaximumPaths_Handler,
    "maximum-paths"_tok, ARG_REST
>;

bool RouterEigrpClassic_MetricMaximumHops_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_MetricMaximumHops = commandAdder<EigrpContext,
    RouterEigrpClassic_MaximumPaths_Handler,
    "metric"_tok, "maximum-hops"_tok, ARG_REST
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

bool RouterEigrpClassic_TimersActive_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_TimersActive = commandAdder<EigrpContext,
    RouterEigrpClassic_TimersActive_Handler,
    "timers"_tok, "active-time"_tok, ARG_REST
>;

bool RouterEigrpClassic_TimersGracefulRestart_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_TimersGracefulRestart = commandAdder<EigrpContext,
    RouterEigrpClassic_TimersGracefulRestart_Handler,
    "timers"_tok, "graceful-restart"_tok, ARG_REST
>;

bool RouterEigrpClassic_TrafficShare_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_TrafficShare = commandAdder<EigrpContext,
    RouterEigrpClassic_TrafficShare_Handler,
    "traffic-share"_tok, ARG_REST
>;

bool RouterEigrpClassic_Variance_Handler(EIGRP_PARAMS);
using RouterEigrpClassic_Variance = commandAdder<EigrpContext,
    RouterEigrpClassic_Variance_Handler,
    "variance"_tok, ARG_REST
>;

using RouterEigrpClassicCommands = CliModeParser<EigrpContext,
    RouterEigrpClassic_AddressFamilyVrf,
    RouterEigrpClassic_AutoSummary,
    RouterEigrpClassic_DefaultMetric,
    RouterEigrpClassic_Distance,
    RouterEigrpClassic_EigrpEventLogSize,
    RouterEigrpClassic_EigrpLogNeighborChanges,
    RouterEigrpClassic_EigrpLogNeighborWarnings,
    RouterEigrpClassic_EigrpRouterId,
    RouterEigrpClassic_EigrpStub,
    RouterEigrpClassic_Exit,
    RouterEigrpClassic_MaximumPaths,
    RouterEigrpClassic_MetricMaximumHops,
    RouterEigrpClassic_MetricWeights,
    RouterEigrpClassic_Neighbor,
    RouterEigrpClassic_Network,
    RouterEigrpClassic_PassiveInterface,
    RouterEigrpClassic_TimersActive,
    RouterEigrpClassic_TimersGracefulRestart,
    RouterEigrpClassic_TrafficShare,
    RouterEigrpClassic_Variance
>;
}

#endif // ROUTER_EIGRP_COMMANDS_H
