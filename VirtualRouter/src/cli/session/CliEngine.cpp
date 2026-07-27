// CliEngine.cpp

#include <sys/resource.h>
#include <Global.h>

#include "CliEngine.h"
#include "CliSession.h"
#include "interface/configs/InterfaceType.hpp"
#include "hardware/HardwareManager.h"

namespace cli
{
CliEngine::CliEngine(core::Global& global, const StartupFiles& stfs, FileSystem& fs, bool test)
    : Configs(fs), global(global)
{
    // Set debug mode based on the input parameter
    global.addRoutingInstance("default");
    if (!test) {
        initEngine(stfs);
    }
}

CliEngine::~CliEngine() 
{
    for (const auto& i : sessions)
    {
        delete i;
    }
    sessions.clear();
}

void CliEngine::initEngine(const StartupFiles& stfs)
{
    // Initialize the base console
    initConfigs(stfs);

    // Initialize default error and carriage return commands
    carriageReturnCommand.name = CARRIAGE_RETURN;

    // ----- Load Command Tree -----
    commandTree.load(fileSystem);
    commandTree.initTree(hwManager);

    /*// ----- Load Config Schema JSON (if used) -----
    configSchema.clear();
    std::string schemaStream;
    if (fileSystem->fileExists(CONFIG_SCHEMA) && fileSystem->readFile(CONFIG_SCHEMA, schemaStream))
    {
        configSchema = nlohmann::ordered_json::parse(schemaStream);
    }
    else
    {
        std::cerr << "Failed to open configuration schema file: " << CONFIG_SCHEMA << std::endl;
        configSchema = json::object();
    }*/

    recoverState(); //TODO
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
    recoverSession.changeModeConfig(new cli::GlobalContext(*recoverSession.modeConfig.modeConfig, global, *global.getRoutingInstance("default")));
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
