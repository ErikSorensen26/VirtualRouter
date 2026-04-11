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
#include "cli/modes/contexts/Context.hpp"
#include "configs/registry/router/EigrpRegistry.h"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpRegistry)

namespace cli
{
bool RouterEigrpTopology_AutoSummary_Handler(EIGRP_PARAMS);
// bool RouterEigrpTopology_DefaultInformation_Handler(EIGRP_PARAMS) //TODO
bool RouterEigrpTopology_DefaultMetric_Handler(EIGRP_PARAMS);
bool RouterEigrpTopology_Distance_Handler(EIGRP_PARAMS);
bool RouterEigrpTopology_EigrpEventLogSize_Handler(EIGRP_PARAMS);
bool RouterEigrpTopology_MaximumPaths_Handler(EIGRP_PARAMS);
bool RouterEigrpTopology_MetricMaximumHops_Handler(EIGRP_PARAMS);
bool RouterEigrpTopology_ActiveTime_Handler(EIGRP_PARAMS);
bool RouterEigrpTopology_TrafficShare_Handler(EIGRP_PARAMS);
bool RouterEigrpTopology_Variance_Handler(EIGRP_PARAMS);

bool RouterEigrpTopologyV4_Exit_Handler(EIGRP_PARAMS);
bool RouterEigrpTopologyV6_Exit_Handler(EIGRP_PARAMS);

#define ROUTER_EIGRP_TOPOLOGY_LIST(X, Y) \
    X(Y, (_COM_, AutoSummary, "auto-summary"_tok)) \
    X(Y, (_COM_, DefaultMetric, "default-metric"_tok)) \
    X(Y, (_COM_, Distance, "distance"_tok, "eigrp"_tok)) \
    X(Y, (_COM_, EigrpEventLogSize, "eigrp"_tok, "event-log-size"_tok)) \
    X(Y, (_COM_, MaximumPaths, "maximum-paths"_tok)) \
    X(Y, (_COM_, MetricMaximumHops, "metric"_tok, "maximum-hops"_tok)) \
    X(Y, (_COM_, ActiveTime, "timers"_tok, "active-timer"_tok)) \
    X(Y, (_COM_, TrafficShare, "traffic-share"_tok)) \
    X(Y, (_COM_, Variance, "variance"_tok)) \

/**
 * @brief Parser for EIGRP topology base filtering and configuration.
 * @ingroup CLI_MODE_PARSERS
 *
 * Enables route filtering (permit/deny) and topology-level redistribution
 * settings for advanced EIGRP control.
 */
DEFINE_CMD_MODE(RouterEigrpTopology, CliMode::None, config::EigrpRegistry, ROUTER_EIGRP_TOPOLOGY_LIST);

#define ROUTER_EIGRP_TOPOLOGY_LIST_V4(X, Y) \
    X(Y, (_COM_, Exit, "exit-af-topology"_tok)) \
    X(Y, (_EXT_, RouterEigrpTopologyCommands))

/**
 * @brief IPv4 topology mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpTopologyV4, CliMode::RouterEigrpTopologyV4, config::EigrpRegistry, ROUTER_EIGRP_TOPOLOGY_LIST_V4);

#define ROUTER_EIGRP_TOPOLOGY_LIST_V6(X, Y) \
    X(Y, (_COM_, Exit, "exit-af-topology"_tok)) \
    X(Y, (_EXT_, RouterEigrpTopologyCommands))

/**
 * @brief IPv6 topology mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpTopologyV6, CliMode::RouterEigrpTopologyV6, config::EigrpRegistry, ROUTER_EIGRP_TOPOLOGY_LIST_V6)
}

#undef ROUTER_EIGRP_TOPOLOGY_LIST
#undef ROUTER_EIGRP_TOPOLOGY_LIST_V4
#undef ROUTER_EIGRP_TOPOLOGY_LIST_V6
#undef EIGRP_PARAMS

#endif // ROUTER_EIGRP_TOPOLOGY_COMMANDS_H
