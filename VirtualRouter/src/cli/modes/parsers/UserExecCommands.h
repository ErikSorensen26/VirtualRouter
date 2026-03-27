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

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/Command.hpp"
#include "cli/modes/contexts/UserExecContext.hpp"

#define USER_EXEC_PARAMS UserExecContext& ctx, const std::vector<std::string>& args

namespace cli
{
bool UserExec_Enable_Handler(USER_EXEC_PARAMS);
using UserExec_Enable = commandAdder<UserExecContext,
    UserExec_Enable_Handler,
    "enable"_tok
>;

bool UserExec_Exit_Handler(USER_EXEC_PARAMS);
using UserExec_Exit = commandAdder<UserExecContext,
    UserExec_Exit_Handler,
    "exit"_tok
>;

/**
 * @brief Parser for User Exec mode commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Provides limited user access with enable and exit commands.
 */
using UserExecCommands = CliModeParser<CliMode::UserExec, UserExecContext,
    UserExec_Enable,
    UserExec_Exit
>;
}

#endif // USER_EXEC_COMMANDS_HPP
