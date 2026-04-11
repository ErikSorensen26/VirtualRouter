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
#include "cli/parser/CliModeParser.hpp"

#define PRIVILEGED_EXEC_PARAMS DEFINE_PARAMS(config::GlobalRegistry)

namespace cli
{
bool PrivilegedExec_ConfigureTerm_Handler(PRIVILEGED_EXEC_PARAMS);
bool PrivilegedExec_Exit_Handler(PRIVILEGED_EXEC_PARAMS);
bool PrivilegedExec_ShowHistory_Handler(PRIVILEGED_EXEC_PARAMS);
bool PrivilegedExec_ShowClock_Handler(PRIVILEGED_EXEC_PARAMS);
bool PrivilegedExec_WriteMem_Handler(PRIVILEGED_EXEC_PARAMS);
bool PrivilegedExec_TerminalWidth_Handler(PRIVILEGED_EXEC_PARAMS);

#define PRIVILEGED_EXEC_LIST(X, Y) \
    X(Y, (COMMAND, ConfigureTerm, "configure"_tok, "terminal"_tok)) \
    X(Y, (COMMAND, Exit, "exit"_tok)) \
    X(Y, (COMMAND, ShowHistory, "show"_tok)) \
    X(Y, (COMMAND, ShowClock, "show"_tok, "clock"_tok)) \
    X(Y, (COMMAND, WriteMem, "write"_tok, "memory"_tok)) \
    X(Y, (COMMAND, TerminalWidth, "terminal"_tok, "width"_tok))

/**
 * @brief Parser for Privileged Exec mode commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates configuration entry point, monitoring (show), and system
 * commands accessible to privileged users.
 */
DEFINE_CMD_MODE(PrivilegedExec, CliMode::PrivilegedExec, config::GlobalRegistry, PRIVILEGED_EXEC_LIST);
}

#undef PRIVILEGED_EXEC_LIST
#undef PRIVILEGED_EXEC_PARAMS

#endif // PRIVILEDGED_EXEC_COMMANDS_HPP
