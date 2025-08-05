// ExecutionManager.h

#ifndef EXECUTION_MANAGER_H
#define EXECUTION_MANAGER_H

#include "TargetProcess.h"
#include "DebuggerOptions.hpp"
#include "FilterEngine.hpp"

class ExecutionManager
{
public:
    ExecutionManager(TargetProcess& proc, const DebuggerOptions& opts, const FilterEngine& filters);
    void run();

private:
    TargetProcess& process;
    const DebuggerOptions& options;
    const FilterEngine& filters;

    void step();
    void printRegisters();
    void handleEvent();
};

#endif // EXECUTION_MANAGER_H
