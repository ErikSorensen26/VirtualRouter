/**
 * @file UserExecCommands.h
 * @brief CLI parser for User Exec mode: basic connectivity commands.
 *
 * Defines commands available in User Exec mode (`>` prompt),
 * providing limited access (enable, exit).
 */

/**
 * @defgroup CLI_MODE_PARSERS CLI Mode Parsers
 * @ingroup CLI_MODES
 * @brief Top-level mode parser headers for Privileged Exec and User Exec modes.
 */

#ifndef USER_EXEC_COMMANDS_HPP
#define USER_EXEC_COMMANDS_HPP

#include "configs/registry/global/GlobalRegistry.h"
#include "cli/modes/contexts/Context.hpp"
#include "cli/modes/Mode.hpp"

namespace cli::execution
{
DEFINE_CMD_EXECUTOR(UserExec, CliMode::UserExec, config::GlobalRegistry);
}

#endif // USER_EXEC_COMMANDS_HPP
