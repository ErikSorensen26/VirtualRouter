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

#include "RouterEigrpCommands.h"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpRegistry)

namespace cli
{
// RouterEigrpAddressFamily_AfInterfaceDefault_Handler(EIGRP_PARAMS) //TODO
bool RouterEigrpAddressFamily_EigrpDefaultRouteTag_Handler(EIGRP_PARAMS);
bool RouterEigrpAddressFamily_EigrpEventLogSize_Handler(EIGRP_PARAMS);
// bool RouterEigrpAddressFamily_EigrpStubSite_Handler(EIGRP_PARAMS); //TODO
bool RouterEigrpAddressFamily_Exit_Handler(EIGRP_PARAMS);
bool RouterEigrpAddressFamily_MaximumPrefix_Handler(EIGRP_PARAMS);
bool RouterEigrpAddressFamily_MetricRibScale_Handler(EIGRP_PARAMS);
bool RouterEigrpAddressFamily_NeighborMaximumPrefix_Handler(EIGRP_PARAMS);
bool RouterEigrpAddressFamily_SoftSia_Handler(EIGRP_PARAMS);

bool RouterEigrpAddressFamilyV4_AfInterface_Handler(EIGRP_PARAMS);
bool RouterEigrpAddressFamilyV6_AfInterface_Handler(EIGRP_PARAMS);
bool RouterEigrpAddressFamilyV4_Topology_Handler(EIGRP_PARAMS);
bool RouterEigrpAddressFamilyV6_Topology_Handler(EIGRP_PARAMS);

#define ROUTER_EIGRP_LIST(X, Y) \
    X(Y, (COMMAND, EigrpDefaultRouteTag, "eigrp"_tok, "default-route-tag"_tok)) \
    X(Y, (COMMAND, EigrpEventLogSize, "eigrp"_tok, "event-log-size"_tok)) \
    X(Y, (COMMAND, Exit, "exit-address-family"_tok)) \
    X(Y, (COMMAND, MaximumPrefix, "maximum-prefix"_tok)) \
    X(Y, (COMMAND, MetricRibScale, "metric"_tok, "rib-scale"_tok)) \
    X(Y, (COMMAND, NeighborMaximumPrefix, "neighbor"_tok, "maximum-prefix"_tok)) \
    X(Y, (COMMAND, SoftSia, "soft-sia"_tok)) \

/**
 * @brief Parser for EIGRPv4 address-family commands (shared IPv4/IPv6).
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates AF-level configuration including networks, neighbors, metrics,
 * and stub mode settings.
 */
DEFINE_CMD_MODE(RouterEigrpAddressFamily, CliMode::None, config::EigrpRegistry, ROUTER_EIGRP_LIST);

#define ROUTER_EIGRP_LIST_V4(X, Y) \
    X(Y, (COMMAND, AfInterface, "af-interface"_tok)) \
    X(Y, (COMMAND, Topology, "topology"_tok, "base"_tok)) \
    X(Y, (INHERIT, RouterEigrpCommands)) \
    X(Y, (INHERIT, RouterEigrpAddressFamilyCommands))

/**
 * @brief IPv4 address-family mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpAddressFamilyV4, CliMode::RouterEigrpAddressFamilyV4, config::EigrpRegistry, ROUTER_EIGRP_LIST_V4)

#define ROUTER_EIGRP_LIST_V6(X, Y) \
    X(Y, (COMMAND, AfInterface, "af-interface"_tok)) \
    X(Y, (COMMAND, Topology, "topology"_tok, "base"_tok)) \
    X(Y, (INHERIT, RouterEigrpCommands)) \
    X(Y, (INHERIT, RouterEigrpAddressFamilyCommands))

/**
 * @brief IPv6 address-family mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpAddressFamilyV6, CliMode::RouterEigrpAddressFamilyV6, config::EigrpRegistry, ROUTER_EIGRP_LIST_V6)
}

#undef ROUTER_EIGR_LIST
#undef ROUTER_EIGR_LIST_V4
#undef ROUTER_EIGR_LIST_V6
#undef EIGRP_PARAMS

#endif // ROUTER_EIGRP_ADDRESS_FAMILY_COMMANDS_H
