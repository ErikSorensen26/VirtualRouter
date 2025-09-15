#include "CliEngine.h"
#include <CommandProcessor.h>
#include <CliSession.h>
#include <SaxJson.hpp>
#include "Mode.hpp"
#include <InterfaceConfigs.h>

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
std::string CliEngine::getMac(InterfaceType type, size_t id)
{
    std::string mac;
    if (type == InterfaceType::ETHERNET && macAddressList.Ethernet.size() >= id)
    {
            mac = OUI + macAddressList.Ethernet[id];
    }
    else if (type == InterfaceType::FAST_ETHERNET && macAddressList.FastEthernet.size() >= id)
    {
            mac = OUI + macAddressList.FastEthernet[id];
    }
    else if (type == InterfaceType::GIGABIT_ETHERNET && macAddressList.GigabitEthernet.size() >= id)
    {
            mac = OUI + macAddressList.GigabitEthernet[id];
    }
    return mac;
}

InterfaceType CliEngine::getInterfaceType(const std::string& type)
{
    if (type == "Dialer") {return InterfaceType::DIALER;}
    else if (type == "Ethernet") {return InterfaceType::ETHERNET;}
    else if (type == "FastEthernet") {return InterfaceType::FAST_ETHERNET;}
    else if (type == "GigabitEthernet") {return InterfaceType::GIGABIT_ETHERNET;}
    else if (type == "Loopback") {return InterfaceType::LOOPBACK;}
    else if (type == "Portchannel") {return InterfaceType::PORT_CHANNEL;}
    else if (type == "Tunnel") {return InterfaceType::TUNNEL;}
    else if (type == "Virtual-Template") {return InterfaceType::VIRTUAL_TEMPLATE;}
    else if (type == "Vlan") {return InterfaceType::VLAN;}
    return InterfaceType::UNDEFINED;
}
