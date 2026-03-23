// UserExecCommands.hpp

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

using UserExecCommands = CliModeParser<CliMode::UserExec, UserExecContext,
    UserExec_Enable,
    UserExec_Exit
>;
}

#endif // USER_EXEC_COMMANDS_HPP
