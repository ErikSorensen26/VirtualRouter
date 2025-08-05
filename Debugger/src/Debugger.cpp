// Debugger.cpp

#include "Debugger.h"
#include "Logger.hpp"
#include "TargetProcess.h"
#include "ExecutionManager.h"
#include "FilterEngine.hpp"

#include <cerrno>
#include <cstring>
#include <sys/ptrace.h>

extern "C" {
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
}

Debugger::Debugger(const DebuggerOptions& opts)
    : options(opts) {}

void Debugger::run()
{
    launchTarget();
    handleExecution();
}

void Debugger::launchTarget()
{
    childPid = fork();
    if (childPid == 0)
    {
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        execl(options.targetPath.c_str(), options.targetPath.c_str(), nullptr);
    }
    else if (childPid > 0)
    {
        int status = 0;
        waitpid(childPid, &status, 0);
        Logger::info("Launched target PID " + std::to_string(childPid));
    }
    else
    {
        Logger::error("Failed to fork target process.");
        exit(1);
    }
}

void Debugger::handleExecution()
{
    TargetProcess proc(childPid);
    FilterEngine filters(options);
    ExecutionManager manager(proc, options, filters);
    manager.run();
}
