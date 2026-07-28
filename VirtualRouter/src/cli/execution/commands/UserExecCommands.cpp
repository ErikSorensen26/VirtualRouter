// UserExecCommands

#include "UserExecCommands.h"
#include "cli/execution/parser/CliModeParser.hpp"
#include "cli/session/CliSession.h"

#define USER_EXEC_PARAMS DEFINE_PARAMS(config::GlobalRegistry)

namespace cli::execution
{
bool UserExec_Enable_Handler(USER_EXEC_PARAMS)
{
    UNUSED(segs);
    ctx.terminal.changeMode<CliMode::PrivilegedExec>(ctx.configs());
    return true;
}

bool UserExec_Exit_Handler(USER_EXEC_PARAMS)
{
    UNUSED(ctx);
    UNUSED(segs);
    exit(1);
    return true;
}

#define USER_EXEC_LIST(X, Y) \
    X(Y, (COMMAND, Enable, "enable"_tok)) \
    X(Y, (COMMAND, Exit, "exit"_tok))

/**
 * @brief Parser for User Exec mode commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Provides limited user access with enable and exit commands.
 */
DEFINE_CMD_MODE(UserExec, config::GlobalRegistry, USER_EXEC_LIST)
}
