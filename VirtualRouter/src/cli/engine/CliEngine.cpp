#include "CliEngine.h"
#include <CommandProcessor.h>
#include <CliSession.h>
#include "Mode.hpp"
#include <InterfaceConfigs.h>
#include <InterfaceType.hpp>
#include <HardwareManager.h>
#include <sys/resource.h>

CliMode CliEngine::defaultMode = CliMode::UserExec;

CliEngine::CliEngine(Global& global, const StartupFiles& stfs, bool test) : Configs(), global(global)
{
    // Set debug mode based on the input parameter
    std::string name = "default";
    global.addRoutingInstance(name);
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
    if (fileSystem->fileExists(COMMAND_TREE_BIN) && fileSystem->readFile(COMMAND_TREE_BIN, fileStream))
    {
        commandTree = nlohmann::ordered_json::from_cbor(
            reinterpret_cast<const uint8_t*>(fileStream.data()),
            reinterpret_cast<const uint8_t*>(fileStream.data()) + fileStream.size()
        );
        initTree();
    }
    else if (fileSystem->fileExists(COMMAND_TREE) && fileSystem->readFile(COMMAND_TREE, fileStream))
    {
        commandTree = nlohmann::ordered_json::parse(
            fileStream.data(),
            fileStream.data() + fileStream.size(),
            nullptr,
            false,
            true
        );

        std::vector<uint8_t> treeBin = nlohmann::ordered_json::to_cbor(commandTree);
        std::string binString(reinterpret_cast<const char*>(treeBin.data()), treeBin.size());
        fileSystem->writeFile(COMMAND_TREE_BIN, binString);
        initTree();
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
        configSchema = nlohmann::ordered_json::parse(schemaStream);
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
        nlohmann::ordered_json& vars = commandTree[VARIABLE_OBJ];

        for (const auto& [type, ifaces] : hwManager->getPhysicalInterfaces())
        {
            std::string typeStr = getInterfaceType(type);
            if (vars.contains(typeStr) && vars[typeStr].is_array())
            {
                size_t size = ifaces.size();
                if (size != 0)
                    vars[typeStr][0][COMMAND_NAME] = "<0-" + std::to_string(size - 1) + ">";
                else
                    vars.erase(typeStr);
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
    recoverSession.changeMode(CliMode::GlobalConfiguration, true);

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

bool CliEngine::isValidCommandDirectory(nlohmann::ordered_json *directory)
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
