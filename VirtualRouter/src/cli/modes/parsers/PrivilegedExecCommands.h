/**
 * @file PrivilegedExecCommands.h
 * @brief CLI parser for Privileged Exec mode: configuration and monitoring commands.
 *
 * Defines commands available in Privileged Exec mode (`#` prompt),
 * including global configuration entry point, show commands, and system operations.
 */

#ifndef PRIVILEGED_EXEC_COMMANDS_HPP
#define PRIVILEGED_EXEC_COMMANDS_HPP

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/Command.hpp"
#include "cli/modes/contexts/PrivilegedExecContext.hpp"

#define PRIVILEGED_EXEC_PARAMS PrivilegedExecContext& ctx, const std::vector<std::string>& args

namespace cli
{
bool PrivilegedExec_ConfigureTerm_Handler(PRIVILEGED_EXEC_PARAMS);
using PrivilegedExec_ConfigureTerm = commandAdder<PrivilegedExecContext,
    PrivilegedExec_ConfigureTerm_Handler,
    "configure"_tok, "terminal"_tok
>;

bool PrivilegedExec_Exit_Handler(PRIVILEGED_EXEC_PARAMS);
using PrivilegedExec_Exit = commandAdder<PrivilegedExecContext,
    PrivilegedExec_Exit_Handler,
    "exit"_tok
>;

bool PrivilegedExec_ShowHistory_Handler(PRIVILEGED_EXEC_PARAMS);
using PrivilegedExec_ShowHistory = commandAdder<PrivilegedExecContext,
    PrivilegedExec_ShowHistory_Handler,
    "show"_tok, "history"_tok
>;

bool PrivilegedExec_ShowClock_Handler(PRIVILEGED_EXEC_PARAMS);
using PrivilegedExec_ShowClock = commandAdder<PrivilegedExecContext,
    PrivilegedExec_ShowClock_Handler,
    "show"_tok, "clock"_tok
>;

bool PrivilegedExec_WriteMem_Handler(PRIVILEGED_EXEC_PARAMS);
using PrivilegedExec_WriteMem = commandAdder<PrivilegedExecContext,
    PrivilegedExec_WriteMem_Handler,
    "write"_tok, "memory"_tok
>;

/**
 * @brief Parser for Privileged Exec mode commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates configuration entry point, monitoring (show), and system
 * commands accessible to privileged users.
 */
using PrivilegedExecCommands = CliModeParser<CliMode::PrivilegedExec, PrivilegedExecContext,
    PrivilegedExec_ConfigureTerm,
    PrivilegedExec_Exit,
    PrivilegedExec_ShowHistory,
    PrivilegedExec_ShowClock,
    PrivilegedExec_WriteMem
>;
}

#endif // PRIVILEDGED_EXEC_COMMANDS_HPP
