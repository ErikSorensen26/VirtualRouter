// CommandProcessing.cpp

#include <sstream>

#include "CliSession.h"
#include "CliEngine.h"

namespace cli
{

std::vector<std::string> CliSession::compileCommandStream(const std::string& command)
{
    // Tokenize by whitespace
    std::vector<std::string> rawTokens;
    {
        std::istringstream ss(command);
        std::string w;
        while (ss >> w) rawTokens.push_back(std::move(w));
    }

    std::vector<std::string> tokens;
    textLine = false;

    for (size_t i = 0; i < rawTokens.size(); i++)
    {
        if (execution.getContext().negate && i == 0)
            continue;

        if (!textLine)
            tokens.push_back(rawTokens[i]);
        else
            tokens.back() += " " + rawTokens[i];

        if (i > 0 && i < commandHistory.size() && commandHistory[i] == "LINE")
            textLine = true;
    }

    return tokens;
}

bool CliSession::executeModeParser(const std::vector<std::string>& tokens)
{
    return execution.execute(tokens);
}

bool CliSession::processConfigPersistence(const std::vector<std::string>& tokens, CliMode preMode)
{
    if (preMode == CliMode::UserExec || preMode == CliMode::PrivilegedExec)
        return false; // No configs modified in exec modes

    if (tokens.empty() || tokens[0] == "error")
        return false;

    if (isExitCommand)
        commandHistory = tokens;

    return true;
}

} // namespace cli
