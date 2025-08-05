// DebuggerOptions.hpp

#ifndef DEBUGGER_OPTIONS_HPP
#define DEBUGGER_OPTIONS_HPP

#include <string>
#include <vector>
#include <regex>
#include <unordered_map>

struct DebuggerOptions
{
    std::string targetPath;
    std::vector<std::string> args;

    // Filters
    std::vector<std::regex> fileIncludes;
    std::vector<std::regex> functionIncludes;

    // Logging options
    bool logFunctionCalls = true;
    bool logReturns = true;
    bool logParameters = true;
    bool logVariables = true;
    bool logEveryLine = false;

    // Variable formatting
    int variableBase = 10; // 10 = decimal, 16 = hex, 256 = byte chunks

    // Runtime
    bool interactiveMode = false;
};

#endif // DEBUGGER_OPTIONS_HPP
