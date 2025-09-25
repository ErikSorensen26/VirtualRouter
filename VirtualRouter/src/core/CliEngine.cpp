#include "CliEngine.h"
#include <CommandProcessor.h>
#include <CliSession.h>
#include <SaxJson.hpp>
#include "Mode.hpp"
#include <InterfaceConfigs.h>
#include <InterfaceType.hpp>
#include <HardwareManager.h>

std::string CliEngine::defaultMode = Mode::userExec;

CliEngine::CliEngine(Global& global, const StartupFiles& stfs, bool test) : Configs(), global(global)
{
    // Set debug mode based on the input parameter
    global.addRoutingInstance("default");
    if (!test) {
        initEngine(stfs);
    }
}

CliEngine::CliEngine(Global& global, const StartupFiles& stfs, IFileSystem* fs, bool test) : Configs(fs), global(global)
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
    errorCommand.name = "<error>";
    carriageReturnCommand.name = "<cr>";

    // Clear current command tree
    commandTree.clear();

    // ----- Load Command Tree JSON via SAX Parsing -----
    std::string fileStream;
    if (fileSystem->fileExists(COMMAND_TREE) && fileSystem->readFile(COMMAND_TREE, fileStream))
    {
        TerminalSaxHandler saxHandler;
        if (json::sax_parse(fileStream, &saxHandler))
        {
            commandTree = saxHandler.result;
            initTree();
        }
        else
        {
            std::cerr << "Failed to parse command tree JSON file: " << COMMAND_TREE << std::endl;
            commandTree = json::object();
        }
    }
    else
    {
        std::cerr << "Failed to open command tree file: " << COMMAND_TREE << std::endl;
        commandTree = json::object();
    }

    // ----- Load Config Schema JSON (if used) -----
    configSchema.clear();
    std::string schemaStream;
    if (fileSystem->fileExists(CONFIG_SCHEMA) && fileSystem->readFile(CONFIG_SCHEMA, schemaStream))
    {
        TerminalSaxHandler schemaSaxHandler;
        if (json::sax_parse(schemaStream, &schemaSaxHandler))
        {
            configSchema = schemaSaxHandler.result;
        }
        else
        {
            std::cerr << "Failed to parse configuration schema file: " << CONFIG_SCHEMA << std::endl;
            configSchema = json::object();
        }
    }
    else
    {
        std::cerr << "Failed to open configuration schema file: " << CONFIG_SCHEMA << std::endl;
        configSchema = json::object();
    }

    recoverState();
}

void CliEngine::initTree()
{

    if (commandTree.contains(VARIABLE_OBJ) && commandTree[VARIABLE_OBJ].contains("interface") && commandTree[VARIABLE_OBJ]["interface"].is_array())
    {
        nlohmann::json& vars = commandTree[VARIABLE_OBJ];

        for (const auto& [type, ifaces] : hwManager->getPhysicalInterfaces())
        {
            std::string typeStr = getInterfaceType(type);
            if (vars.contains(typeStr) && vars[typeStr].is_array())
            {
                vars[typeStr][0][COMMAND_NAME] = "<0-" + std::to_string(ifaces.size() - 1) + ">";
            }
        }
    }
}

CliSession* CliEngine::createSession(bool debug)
{
    sessions.push_back(new CliSession(*this, debug));
    return sessions.back();
}

CliSession* CliEngine::createSession(IConsole* console)
{
    sessions.push_back(new CliSession(*this, console));
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
    // Retrieve the list of saved commands from the JSON recovery system
    std::vector<std::string> savedCommands = recoverConfigs();
	CliSession recoverSession(*this);

    // Set initial mode for command recovery
    recoverSession.changeMode(Mode::globalConfiguration, true);

    // Execute each saved command to restore the terminal's state
    for (std::string& command : savedCommands) {
        recoverSession.initializeProcessingState();
        recoverSession.executeCommand(command);  // Execute the command
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Add a delay for stability
    }
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

bool CliEngine::isValidCommandDirectory(nlohmann::json *directory)
{
    if (directory && directory->is_object())
    {
        return directory->contains("subcommands");
    }
    return false;
}

std::string CliEngine::maskInput(const std::string& prefix, std::string original)
{
    if (prefix.length() > original.length())
    {
        return original;
    }

    // Replace the beginning of the original string with the prefix
    std::copy(prefix.begin(), prefix.end(), original.begin());

    return original;
}
