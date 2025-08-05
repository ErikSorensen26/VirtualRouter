// Debugger.h

#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "DebuggerOptions.hpp"
#include <sys/types.h>
#include <string>

class Debugger
{
public:
    explicit Debugger(const DebuggerOptions& options);
    void run();

private:
    DebuggerOptions options;
    pid_t childPid;

    void launchTarget();
    void handleExecution();
};

#endif // DEBUGGER_H
