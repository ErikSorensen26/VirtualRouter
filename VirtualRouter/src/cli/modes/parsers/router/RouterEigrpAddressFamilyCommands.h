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

#include "configs/registry/router/EigrpRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(RouterEigrpAddressFamily, CliMode::None, config::EigrpRegistry);
DEFINE_CMD_EXECUTOR(RouterEigrpAddressFamilyV4, CliMode::RouterEigrpAddressFamilyV4, config::EigrpRegistry);
DEFINE_CMD_EXECUTOR(RouterEigrpAddressFamilyV6, CliMode::RouterEigrpAddressFamilyV6, config::EigrpRegistry);
}

#endif // ROUTER_EIGRP_ADDRESS_FAMILY_COMMANDS_H
