// ExecutionContext.hpp

#ifndef EXECUTION_CONTEXT_HPP
#define EXECUTION_CONTEXT_HPP

#include "cli/execution/Executor.hpp"

#include "cli/modes/parsers/global/GlobalCommands.h"
#include "cli/modes/parsers/interface/InterfaceCommands.h"
#include "cli/modes/parsers/PrivilegedExecCommands.h"
#include "cli/modes/parsers/UserExecCommands.h"

#include "cli/modes/parsers/router/RouterEigrpClassicCommands.h"
#include "cli/modes/parsers/router/RouterEigrpClassicVrfCommands.h"
#include "cli/modes/parsers/router/RouterEigrpNamedCommands.h"
#include "cli/modes/parsers/router/RouterEigrpAddressFamilyCommands.h"
#include "cli/modes/parsers/router/RouterEigrpInterfaceCommands.h"
#include "cli/modes/parsers/router/RouterEigrpTopologyCommands.h"

namespace Cli
{
using ExecutionManager = ::Executor<
    UserExecCommands,
    PrivilegedExecCommands,
    GlobalCommands,
    InterfaceCommands,
    RouterEigrpClassicCommandsV4,
    RouterEigrpClassicCommandsV6,
    RouterEigrpClassicVrfCommands,
    /* RouterEigrpClassicV6Commands */
    RouterEigrpNamedCommands,
    RouterEigrpAddressFamilyV4Commands,
    RouterEigrpAddressFamilyV6Commands,
    RouterEigrpInterfaceV4Commands,
    RouterEigrpInterfaceV6Commands,
    RouterEigrpTopologyV4Commands,
    RouterEigrpTopologyV6Commands
>;
}

#endif // EXECUTION_CONTEXT_HPP
