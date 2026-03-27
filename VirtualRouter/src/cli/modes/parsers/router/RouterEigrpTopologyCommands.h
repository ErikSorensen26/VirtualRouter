/**
 * @file RouterEigrpTopologyCommands.h
 * @brief CLI parser for EIGRP topology base commands.
 *
 * Defines topology-level filtering and configuration including
 * route filtering, prefix lists, and metric redistribution settings.
 */

#ifndef ROUTER_EIGRP_TOPOLOGY_COMMANDS_H
#define ROUTER_EIGRP_TOPOLOGY_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/Command.hpp"
#include "cli/modes/contexts/EigrpContext.hpp"

namespace cli
{
bool RouterEigrpTopology_AutoSummary_Handler(EIGRP_PARAMS);
using RouterEigrpTopology_AutoSummary = commandAdder<EigrpContext,
    RouterEigrpTopology_AutoSummary_Handler,
    "auto-summary"_tok
>;

// bool RouterEigrpTopology_DefaultInformation_Handler(EIGRP_PARAMS) //TODO

bool RouterEigrpTopology_DefaultMetric_Handler(EIGRP_PARAMS);
using RouterEigrpTopology_DefaultMetric = commandAdder<EigrpContext,
    RouterEigrpTopology_DefaultMetric_Handler,
    "default-metric"_tok, ARG_REST
>;

bool RouterEigrpTopology_Distance_Handler(EIGRP_PARAMS);
using RouterEigrpTopology_Distance = commandAdder<EigrpContext,
    RouterEigrpTopology_Distance_Handler,
    "distance"_tok, ARG_REST
>;

bool RouterEigrpTopology_EigrpEventLogSize_Handler(EIGRP_PARAMS);
using RouterEigrpTopology_EigrpEventLogSize = commandAdder<EigrpContext,
    RouterEigrpTopology_EigrpEventLogSize_Handler,
    "eigrp"_tok, "event-log-size"_tok, ARG_REST
>;

bool RouterEigrpTopology_Exit_Handler(EIGRP_PARAMS);
using RouterEigrpTopology_Exit = commandAdder<EigrpContext,
    RouterEigrpTopology_Exit_Handler,
    "exit-af-topology"_tok
>;

bool RouterEigrpTopology_MaximumPaths_Handler(EIGRP_PARAMS);
using RouterEigrpTopology_MaximumPaths = commandAdder<EigrpContext,
    RouterEigrpTopology_MaximumPaths_Handler,
    "maximum-paths"_tok, ARG_REST
>;

bool RouterEigrpTopology_MetricMaximumHops_Handler(EIGRP_PARAMS);
using RouterEigrpTopology_MetricMaximumHops = commandAdder<EigrpContext,
    RouterEigrpTopology_MetricMaximumHops_Handler,
    "metric"_tok, "maximum-hops"_tok, ARG_REST
>;

bool RouterEigrpTopology_ActiveTime_Handler(EIGRP_PARAMS);
using RouterEigrpTopology_ActiveTime = commandAdder<EigrpContext,
    RouterEigrpTopology_ActiveTime_Handler,
    "timers"_tok, "active-time"_tok, ARG_REST
>;

bool RouterEigrpTopology_TrafficShare_Handler(EIGRP_PARAMS);
using RouterEigrpTopology_TrafficShare = commandAdder<EigrpContext,
    RouterEigrpTopology_TrafficShare_Handler,
    "traffic-share"_tok, ARG_REST
>;

bool RouterEigrpTopology_Variance_Handler(EIGRP_PARAMS);
using RouterEigrpTopology_Variance = commandAdder<EigrpContext,
    RouterEigrpTopology_Variance_Handler,
    "variance"_tok, ARG_REST
>;

/**
 * @brief Parser for EIGRP topology base filtering and configuration.
 * @ingroup CLI_MODE_PARSERS
 *
 * Enables route filtering (permit/deny) and topology-level redistribution
 * settings for advanced EIGRP control.
 */
using RouterEigrpTopologyCommands = CliModeParser<CliMode::None, EigrpContext,
    RouterEigrpTopology_AutoSummary,
    RouterEigrpTopology_DefaultMetric,
    RouterEigrpTopology_Distance,
    RouterEigrpTopology_EigrpEventLogSize,
    RouterEigrpTopology_Exit,
    RouterEigrpTopology_MaximumPaths,
    RouterEigrpTopology_MetricMaximumHops,
    RouterEigrpTopology_ActiveTime,
    RouterEigrpTopology_TrafficShare,
    RouterEigrpTopology_Variance
>;

/**
 * @brief IPv4 topology mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
using RouterEigrpTopologyV4Commands = CliModeParser<CliMode::RouterEigrpTopologyV4, EigrpContext,
    RouterEigrpTopologyCommands
>;

/**
 * @brief IPv6 topology mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
using RouterEigrpTopologyV6Commands = CliModeParser<CliMode::RouterEigrpTopologyV6, EigrpContext,
    RouterEigrpTopologyCommands
>;
}

#endif // ROUTER_EIGRP_TOPOLOGY_COMMANDS_H
