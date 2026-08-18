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
    /*// Retrieve the list of saved commands from the JSON recovery system
    std::vector<std::string> savedCommands = recoverConfigs();
	CliSession recoverSession(*this);

    // Set initial mode for command recovery
    recoverSession.changeModeConfig(new cli::GlobalContext(*recoverSession.modeConfig.modeConfig, global, *global.getRoutingInstance(DEFAULT_VRF)));
    recoverSession.changeMode(CliMode::GlobalConfiguration, true);

    // Execute each saved command to restore the terminal's state
    for (std::string& command : savedCommands) {
        recoverSession.initializeProcessingState();
        recoverSession.executeCommand(command);  // Execute the command
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Add a delay for stability
    }*/
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
