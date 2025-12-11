// PriviledgedExecCommands.hpp

#ifndef PRIVILEGED_EXEC_COMMANDS_HPP
#define PRIVILEGED_EXEC_COMMANDS_HPP

#include <CliModeParser.hpp>
#include <PrivilegedExecContext.hpp>

#define PRIVILEGED_EXEC_PARAMS PrivilegedExecContext& ctx, const std::vector<std::string>& args

namespace Cli
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

using PrivilegedExecCommands = CliModeParser<PrivilegedExecContext,
    PrivilegedExec_ConfigureTerm,
    PrivilegedExec_Exit,
    PrivilegedExec_ShowHistory,
    PrivilegedExec_ShowClock,
    PrivilegedExec_WriteMem
>;
}

#endif // PRIVILEDGED_EXEC_COMMANDS_HPP
