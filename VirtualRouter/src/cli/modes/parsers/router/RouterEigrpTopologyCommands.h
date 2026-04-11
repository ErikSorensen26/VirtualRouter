/**
 * @file RouterEigrpTopologyCommands.h
 * @brief CLI parser for EIGRP topology base commands.
 *
 * Defines topology-level filtering and configuration including
 * route filtering, prefix lists, and metric redistribution settings.
 */

#ifndef ROUTER_EIGRP_TOPOLOGY_COMMANDS_H
#define ROUTER_EIGRP_TOPOLOGY_COMMANDS_H

#include "configs/registry/router/EigrpRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(RouterEigrpTopology, CliMode::None, config::EigrpRegistry);
DEFINE_CMD_EXECUTOR(RouterEigrpTopologyV4, CliMode::RouterEigrpTopologyV4, config::EigrpRegistry);
DEFINE_CMD_EXECUTOR(RouterEigrpTopologyV6, CliMode::RouterEigrpTopologyV6, config::EigrpRegistry);
}

#endif // ROUTER_EIGRP_TOPOLOGY_COMMANDS_H
