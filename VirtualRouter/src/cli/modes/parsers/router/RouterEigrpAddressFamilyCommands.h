/**
 * @file RouterEigrpAddressFamilyCommands.h
 * @brief CLI parser for EIGRP address-family sub-mode commands.
 *
 * Defines commands available within an EIGRP address family (IPv4/IPv6),
 * including interface configuration (af-interface), network/neighbor setup,
 * metric/weight tuning, DUAL stub mode, and topology filtering.
 */

#ifndef ROUTER_EIGRP_ADDRESS_FAMILY_COMMANDS_H
#define ROUTER_EIGRP_ADDRESS_FAMILY_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/Command.hpp"
#include "cli/modes/contexts/EigrpContext.hpp"

namespace cli
{
bool RouterEigrpAddressFamily_AfInterface_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_AfInterface = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_AfInterface_Handler,
    "af-interface"_tok
>;

// RouterEigrpAddressFamily_AfInterfaceDefault_Handler(EIGRP_PARAMS) //TODO

bool RouterEigrpAddressFamily_EigrpDefaultRouteTag_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_EigrpDefaultRouteTag = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_EigrpDefaultRouteTag_Handler,
    "eigrp"_tok, "default-route-tag"_tok
>;

bool RouterEigrpAddressFamily_EigrpEventLogSize_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_EigrpEventLogSize = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_EigrpEventLogSize_Handler,
    "eigrp"_tok, "event-log-size"_tok
>;

bool RouterEigrpAddressFamily_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_EigrpLogNeighborChanges = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_EigrpLogNeighborChanges_Handler,
    "eigrp"_tok, "log-neighbor-changes"_tok
>;

bool RouterEigrpAddressFamily_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_EigrpLogNeighborWarnings = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_EigrpLogNeighborWarnings_Handler,
    "eigrp"_tok, "log-neighbor-warnings"_tok
>;

bool RouterEigrpAddressFamily_EigrpRouterId_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_EigrpRouterId = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_EigrpRouterId_Handler,
    "eigrp"_tok, "router-id"_tok
>;

bool RouterEigrpAddressFamily_EigrpStub_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_EigrpStub = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_EigrpStub_Handler,
    "eigrp"_tok, "stub"_tok
>;

// bool RouterEigrpAddressFamily_EigrpStubSite_Handler(EIGRP_PARAMS); //TODO

bool RouterEigrpAddressFamily_Exit_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_Exit = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_Exit_Handler,
    "exit-address-family"_tok
>;

bool RouterEigrpAddressFamily_MaximumPrefix_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_MaximumPrefix = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_MaximumPrefix_Handler,
    "maximum-prefix"_tok
>;

bool RouterEigrpAddressFamily_MetricRibScale_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_MetricRibScale = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_MetricRibScale_Handler,
    "metric"_tok, "rib-scale"_tok
>;

bool RouterEigrpAddressFamily_MetricWeights_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_MetricWeights = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_MetricWeights_Handler,
    "metric"_tok, "weights"_tok
>;

bool RouterEigrpAddressFamily_Neighbor_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_Neighbor = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_Neighbor_Handler,
    "neighbor"_tok
>;

bool RouterEigrpAddressFamily_Network_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_Network = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_Network_Handler,
    "network"_tok
>;

// bool RouterEigrpAddressFamily_Shutdown_Handler(EIGRP_PARAMS); //TODO

bool RouterEigrpAddressFamily_SoftSia_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_SoftSia = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_SoftSia_Handler,
    "soft-sia"_tok
>;

bool RouterEigrpAddressFamily_TimersGracefulRestart_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_TimersGracefulRestart = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_TimersGracefulRestart_Handler,
    "timers"_tok, "graceful-restart"_tok, "purge-time"_tok
>;

bool RouterEigrpAddressFamily_Topology_Handler(EIGRP_PARAMS);
using RouterEigrpAddressFamily_Topology = commandAdder<EigrpContext,
    RouterEigrpAddressFamily_Topology_Handler,
    "topology"_tok
>;

/**
 * @brief Parser for EIGRP address-family commands (shared IPv4/IPv6).
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates AF-level configuration including networks, neighbors, metrics,
 * and stub mode settings.
 */
using RouterEigrpAddressFamilyCommands = CliModeParser<CliMode::None, EigrpContext,
    RouterEigrpAddressFamily_AfInterface,
    RouterEigrpAddressFamily_EigrpDefaultRouteTag,
    RouterEigrpAddressFamily_EigrpEventLogSize,
    RouterEigrpAddressFamily_EigrpLogNeighborChanges,
    RouterEigrpAddressFamily_EigrpLogNeighborWarnings,
    RouterEigrpAddressFamily_EigrpRouterId,
    RouterEigrpAddressFamily_EigrpStub,
    RouterEigrpAddressFamily_Exit,
    RouterEigrpAddressFamily_MaximumPrefix,
    RouterEigrpAddressFamily_MetricRibScale,
    RouterEigrpAddressFamily_MetricWeights,
    RouterEigrpAddressFamily_Neighbor,
    RouterEigrpAddressFamily_Network,
    RouterEigrpAddressFamily_SoftSia,
    RouterEigrpAddressFamily_TimersGracefulRestart,
    RouterEigrpAddressFamily_Topology
>;

/**
 * @brief IPv4 address-family mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
using RouterEigrpAddressFamilyV4Commands = CliModeParser<CliMode::RouterEigrpAddressFamilyV4, EigrpContext,
    RouterEigrpAddressFamilyCommands
>;

/**
 * @brief IPv6 address-family mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
using RouterEigrpAddressFamilyV6Commands = CliModeParser<CliMode::RouterEigrpAddressFamilyV6, EigrpContext,
    RouterEigrpAddressFamilyCommands
>;
}

#endif // ROUTER_EIGRP_ADDRESS_FAMILY_COMMANDS_H
