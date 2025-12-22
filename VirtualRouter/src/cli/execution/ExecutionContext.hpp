// ExecutionContext.hpp

#ifndef EXECUTION_CONTEXT_HPP
#define EXECUTION_CONTEXT_HPP

#include "Executor.hpp"

#include <GlobalCommands.h>
#include <InterfaceCommands.h>
#include <PrivilegedExecCommands.h>
#include <UserExecCommands.h>

#include <RouterEigrpClassicCommands.h>
#include <RouterEigrpClassicVrfCommands.h>
#include <RouterEigrpNamedCommands.h>
#include <RouterEigrpAddressFamilyCommands.h>
#include <RouterEigrpInterfaceCommands.h>
#include <RouterEigrpTopologyCommands.h>

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
