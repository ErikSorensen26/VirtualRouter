// UserExecCommands.hpp

#ifndef USER_EXEC_COMMANDS_HPP
#define USER_EXEC_COMMANDS_HPP

#include <CliSession.h>
#include <CliModeParser.hpp>

#define USER_EXEC_PARAMS UserExecContext& ctx, const std::vector<std::string>& args

namespace Cli
{
struct UserExecContext
{
    CliSession& terminal;
};

using UserExec_Enable = commandAdder<UserExecContext,
    [](USER_EXEC_PARAMS) {
        UNUSED(args);
        ctx.terminal.changeMode(CliMode::PrivilegedExec);
    },
    "enable"_tok
>;

using UserExec_Exit = commandAdder<UserExecContext,
    [](USER_EXEC_PARAMS) {
        UNUSED(ctx);
        UNUSED(args);
        exit(1);
    },
    "exit"_tok
>;

#undef USER_EXEC_PARAMS

using UserExecCommands = CliModeParser<UserExecContext,
    UserExec_Enable,
    UserExec_Exit
>;
}

#endif // USER_EXEC_COMMANDS_HPP
