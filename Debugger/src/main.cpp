#include "DebuggerOptions.hpp"
#include "Logger.hpp"
#include "FilterEngine.hpp"

/**
 * Steps, Breakpoints, Continues
 * Way to filter files that will be debugged
 * Way to filter specific sections of code that will be debugged (e.g. functions)
 * Optional: Log every function in filtered files thisisfunction(type1 param1name value, type2 param2name value)
 * Optional: Log every line of code executed in filtered files with values
 * Dump all in-scope variables
 * be able to add own base for showing variables (e.g. 10 for decimal, 16 for decimal, 256 for bytes)
 * designed to support tui interface
 */

int main(int argc, char* argv[])
{
    DebuggerOptions opts;
    opts.targetPath = "../../Tests";
    opts.logFunctionCalls = true;
    opts.logReturns = true;
    opts.variableBase = 16;

    opts.fileIncludes.emplace_back(".*src/.");
    opts.functionIncludes.emplace_back("main");

    FilterEngine filters(opts);

    Logger::info("Debugger started.");
    Logger::info(std::string("Allow 'src/main.cpp'?") + (filters.allowFile("src/main.cpp") ? "yes" : "no"));
    Logger::info(std::string("Allow 'main'?") + (filters.allowFunction("main") ? "yes" : "no"));
}
