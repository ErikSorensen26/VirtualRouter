/**
 * @file PrivilegedExecCommands.h
 * @brief CLI parser for Privileged Exec mode: configuration and monitoring commands.
 *
 * Defines commands available in Privileged Exec mode (`#` prompt),
 * including global configuration entry point, show commands, and system operations.
 */

#ifndef PRIVILEGED_EXEC_COMMANDS_HPP
#define PRIVILEGED_EXEC_COMMANDS_HPP

#include "configs/registry/global/GlobalRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli
{
DEFINE_CMD_EXECUTOR(PrivilegedExec, CliMode::PrivilegedExec, config::GlobalRegistry);
}

#endif // PRIVILEDGED_EXEC_COMMANDS_HPP
