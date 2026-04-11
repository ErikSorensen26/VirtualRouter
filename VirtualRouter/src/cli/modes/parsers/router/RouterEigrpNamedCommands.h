/**
 * @file RouterEigrpNamedCommands.h
 * @brief CLI parser for EIGRP named mode (MD5-era) commands.
 *
 * Defines commands for EIGRP named mode configuration,
 * including address-family setup, timers, logging, and process-wide settings.
 */

#ifndef ROUTER_EIGRP_NAMED_COMMANDS_H
#define ROUTER_EIGRP_NAMED_COMMANDS_H

#include "configs/registry/router/EigrpRegistry.h"
#include "cli/parser/CliModeParser.hpp"
#include "cli/modes/contexts/Context.hpp"

#define EIGRP_NAMED_PARAMS DEFINE_PARAMS(config::EigrpNamedRegistry)

namespace cli
{
bool RouterEigrpNamed_AddressFamilyIPv4_Handler(EIGRP_NAMED_PARAMS);
bool RouterEigrpNamed_AddressFamilyIPv6_Handler(EIGRP_NAMED_PARAMS);
bool RouterEigrpNamed_Exit_Handler(EIGRP_NAMED_PARAMS);

#define ROUTER_EIGRP_NAMED_LIST(X, Y) \
    X(Y, (COMMAND, AddressFamilyIPv4, "address-family"_tok, "ipv4"_tok)) \
    X(Y, (COMMAND, AddressFamilyIPv6, "address-family"_tok, "ipv6"_tok)) \
    X(Y, (COMMAND, Exit, "exit"_tok))

/**
 * @brief Parser for EIGRP named mode (MD5-era) configuration commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates address-family entry, logging, metrics, topology access,
 * and named-mode-specific settings.
 */
DEFINE_CMD_MODE(RouterEigrpNamed, CliMode::RouterEigrpNamed, config::EigrpNamedRegistry, ROUTER_EIGRP_NAMED_LIST);
}

#undef ROUTER_EIGRP_NAMED_LIST
#undef EIGRP_NAMED_PARAMS

#endif // ROUTER_EIGRP_NAMED_COMMANDS_H
