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
#include "cli/parser/CliModeParser.hpp"

#define USER_EXEC_PARAMS DEFINE_PARAMS(config::GlobalRegistry)

namespace cli
{
bool UserExec_Enable_Handler(USER_EXEC_PARAMS);
bool UserExec_Exit_Handler(USER_EXEC_PARAMS);

#define USER_EXEC_LIST(X, Y) \
    X(Y, (COMMAND, Enable, "enable"_tok)) \
    X(Y, (COMMAND, Exit, "exit"_tok))

/**
 * @brief Parser for User Exec mode commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Provides limited user access with enable and exit commands.
 */
DEFINE_CMD_MODE(UserExec, CliMode::UserExec, config::GlobalRegistry, USER_EXEC_LIST)
}

#endif // USER_EXEC_COMMANDS_HPP
