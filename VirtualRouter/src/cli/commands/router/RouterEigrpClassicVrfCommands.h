// RouterEigrpClassicVrfCommands.h

#ifndef ROUTER_EIGRP_CLASSIC_VRF_COMMANDS_H
#define ROUTER_EIGRP_CLASSIC_VRF_COMMANDS_H

#include <EigrpContext.hpp>
#include <CliModeParser.hpp>

namespace Cli
{
bool RouterEigrpClassicVrf_AutoSummary_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_AutoSummary = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_AutoSummary_Handler,
    "auto-summary"_tok
>;

//bool RouterEigrpClassicVrf_DefaultInformation_Handler(EIGRP_PARAMS) {} //TODO

bool RouterEigrpClassicVrf_DefaultMetric_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_DefaultMetric = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_DefaultMetric_Handler,
    "default-metric"_tok, ARG_REST
>;

bool RouterEigrpClassicVrf_Distance_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_Distance = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_Distance_Handler,
    "distance"_tok, ARG_REST
>;

bool RouterEigrpClassicVrf_EigrpEventLogSize_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_EigrpEventLogSize = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_EigrpEventLogSize_Handler,
    "eigrp"_tok, "event-log-size"_tok, ARG_REST
>;

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

bool RouterEigrpClassicVrf_MaximumPaths_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_MaximumPaths = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_MaximumPaths_Handler,
    "maximum-paths"_tok, ARG_REST
>;

bool RouterEigrpClassicVrf_MetricMaximumHops_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_MetricMaximumHops = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_MaximumPaths_Handler,
    "metric"_tok, "maximum-hops"_tok, ARG_REST
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

bool RouterEigrpClassicVrf_TimersActive_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_TimersActive = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_TimersActive_Handler,
    "timers"_tok, "active-time"_tok, ARG_REST
>;

bool RouterEigrpClassicVrf_TimersGracefulRestart_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_TimersGracefulRestart = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_TimersGracefulRestart_Handler,
    "timers"_tok, "graceful-restart"_tok, ARG_REST
>;

bool RouterEigrpClassicVrf_TrafficShare_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_TrafficShare = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_TrafficShare_Handler,
    "traffic-share"_tok, ARG_REST
>;

bool RouterEigrpClassicVrf_Variance_Handler(EIGRP_PARAMS);
using RouterEigrpClassicVrf_Variance = commandAdder<EigrpContext,
    RouterEigrpClassicVrf_Variance_Handler,
    "variance"_tok, ARG_REST
>;

using RouterEigrpClassicVrfCommands = CliModeParser<EigrpContext,
    RouterEigrpClassicVrf_AutoSummary,
    RouterEigrpClassicVrf_DefaultMetric,
    RouterEigrpClassicVrf_Distance,
    RouterEigrpClassicVrf_EigrpEventLogSize,
    RouterEigrpClassicVrf_EigrpLogNeighborChanges,
    RouterEigrpClassicVrf_EigrpLogNeighborWarnings,
    RouterEigrpClassicVrf_EigrpRouterId,
    RouterEigrpClassicVrf_EigrpStub,
    RouterEigrpClassicVrf_Exit,
    RouterEigrpClassicVrf_MaximumPaths,
    RouterEigrpClassicVrf_MetricMaximumHops,
    RouterEigrpClassicVrf_MetricWeights,
    RouterEigrpClassicVrf_Neighbor,
    RouterEigrpClassicVrf_Network,
    RouterEigrpClassicVrf_PassiveInterface,
    RouterEigrpClassicVrf_TimersActive,
    RouterEigrpClassicVrf_TimersGracefulRestart,
    RouterEigrpClassicVrf_TrafficShare,
    RouterEigrpClassicVrf_Variance
>;
}

#endif // ROUTER_EIGRP_CLASSIC_VRF_COMMANDS_H
