/**
 * @file RouterEigrpCommands.h
 * @brief CLI parser for EIGRP commands.
 *
 * Defines commands for EIGRP classic (flat) configuration model,
 * including network/neighbor setup, address-family entry point,
 * metrics, logging, and topology access.
 */

#ifndef ROUTER_EIGRP_COMMANDS_H
#define ROUTER_EIGRP_COMMANDS_H

#include "configs/registry/router/EigrpRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli::execution
{
DEFINE_CMD_EXECUTOR(RouterEigrp, CliMode::None, config::EigrpRegistry);
}

#endif // ROUTER_EIGRP_COMMANDS_H
