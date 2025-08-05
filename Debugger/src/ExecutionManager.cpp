// ExecutionManager.cpp

#include "ExecutionManager.h"
#include "Logger.hpp"
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <unistd.h>
#include <iostream>

ExecutionManager::ExecutionManager(TargetProcess& proc, const DebuggerOptions& opts, const FilterEngine& filters)
    : process(proc), options(opts), filters(filters) {}

void ExecutionManager::run()
{
    while (true)
    {
        int status = 0;
        waitpid(process.getPid(), &status, 0);

        if (WIFEXITED(status))
        {
            Logger::info("Target exited.");
            break;
        }

        handleEvent();
        step();
    }
}

void ExecutionManager::step()
{
    ptrace(PTRACE_SINGLESTEP, process.getPid(), nullptr, nullptr);
}

void ExecutionManager::handleEvent()
{
    user_regs_struct regs;
    ptrace(PTRACE_GETREGS, process.getPid(), nullptr, &regs);
    Logger::info("Stepped to RIP = " + Logger::toBase(regs.rip, options.variableBase));

    // TODO use DWARF parser to map RIP to source line
    // TODO detect function entry and return
}
