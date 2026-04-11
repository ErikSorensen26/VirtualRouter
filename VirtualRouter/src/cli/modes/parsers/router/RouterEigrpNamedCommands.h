/**
 * @file RouterEigrpNamedCommands.h
 * @brief CLI parser for EIGRP named mode (MD5-era) commands.
 *
 * Defines commands for EIGRP named mode configuration,
 * including address-family setup, timers, logging, and process-wide settings.
 */

#ifndef ROUTER_EIGRP_NAMED_COMMANDS_H
#define ROUTER_EIGRP_NAMED_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/modes/contexts/Context.hpp"
#include "configs/registry/global/GlobalRegistry.h"

#define GLOBAL_PARAMS DEFINE_PARAMS(config::GlobalRegistry)

namespace cli
{
bool RouterEigrpNamed_AddressFamilyIPv4_Handler(GLOBAL_PARAMS);
bool RouterEigrpNamed_AddressFamilyIPv6_Handler(GLOBAL_PARAMS);
bool RouterEigrpNamed_Exit_Handler(GLOBAL_PARAMS);

#define ROUTER_EIGRP_NAMED_LIST(X, Y) \
    X(Y, (_COM_, AddressFamilyIPv4, "address-family"_tok, "ipv4"_tok)) \
    X(Y, (_COM_, AddressFamilyIPv6, "address-family"_tok, "ipv6"_tok)) \
    X(Y, (_COM_, Exit, "exit"_tok))

/**
 * @brief Parser for EIGRP named mode (MD5-era) configuration commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates address-family entry, logging, metrics, topology access,
 * and named-mode-specific settings.
 */
DEFINE_CMD_MODE(RouterEigrpNamed, CliMode::RouterEigrpNamed, config::GlobalRegistry, ROUTER_EIGRP_NAMED_LIST);
}

#undef ROUTER_EIGRP_NAMED_LIST
#undef GLOBAL_PARAMS

#endif // ROUTER_EIGRP_NAMED_COMMANDS_H
