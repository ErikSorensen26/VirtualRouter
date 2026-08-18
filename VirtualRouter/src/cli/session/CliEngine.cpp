// CliEngine.cpp

#include <sys/resource.h>
#include <Global.h>

#include "CliEngine.h"
#include "CliSession.h"

namespace cli
{
CliEngine::CliEngine(core::Global& global, const StartupFiles& stfs, tree::CommandTree& tree)
    : global(global), commandTree(tree)
{
    commandTree.applyPortCounts(tree::CommandTree::readPortCounts(stfs.hwConfigFile));
}

CliEngine::~CliEngine() 
{
    for (const auto& i : sessions)
    {
        delete i;
    }
    sessions.clear();
}

CliSession* CliEngine::createSession(bool debug)
{
    sessions.push_back(new CliSession(*this, controller, debug));
    return sessions.back();
}

CliSession* CliEngine::createSession(ConsoleController& ctr)
{
    sessions.push_back(new CliSession(*this, ctr));
    return sessions.back();
}

void CliEngine::clearSessions()
{
    for (auto& session : sessions)
    {
        delete session;
    }
    sessions.clear();
}

void CliEngine::recoverState()
{
}

bool CliEngine::isNumeric(const std::string &input)
{
    if (input.empty() || (!std::isdigit(input[0]) && input[0] != '-' && input[0] != '+'))
    {
        return false;
    }

    char *endPtr;
    std::strtol(input.c_str(), &endPtr, 10);

    return (*endPtr == '\0');
}

std::string& CliEngine::maskInput(std::string_view prefix, std::string& original)
{
    if (prefix.length() > original.length())
        return original;
    // Replace the beginning of the original string with the prefix
    std::copy(prefix.begin(), prefix.end(), original.begin());
    return original;
}
}
