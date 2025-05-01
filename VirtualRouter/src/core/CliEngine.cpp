#include "CliEngine.h"

#include <CommandProcessor.h>
#include <CliSession.h>
#include <SaxJson.hpp>
#include "Mode.hpp"

std::string CliEngine::defaultMode = Mode::userExec;

CliEngine::CliEngine() : Configs()
{
    // Set debug mode based on the input parameter
    auto& global = Global::getInstance();
    auto vrf = global.addRoutingInstance("default");
    vrf->enabledAddressFamilies.insert(AddressFamily::IPv4); // IPv4 Enabled by default
    initEngine();
}

CliEngine::~CliEngine() 
{
    for (const auto& i : sessions)
    {
        delete i;
    }
    sessions.clear();
}

void CliEngine::initEngine()
{
    // Initialize the base console
    initConfigs();

    // Initialize default error and carriage return commands
    errorCommand.name = "<error>";
    carriageReturnCommand.name = "<cr>";

    // Clear current command tree
    commandTree.clear();

    // ----- Load Command Tree JSON via SAX Parsing -----
    std::ifstream fileStream(COMMAND_TREE);
    if (fileStream.is_open())
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
        fileStream.close();
    }
    else
    {
        std::cerr << "Failed to open command tree file: " << COMMAND_TREE << std::endl;
        commandTree = json::object();
    }

    // ----- Load Config Schema JSON (if used) -----
    configSchema.clear();
    std::ifstream schemaStream(CONFIG_SCHEMA);
    if (schemaStream.is_open())
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
        schemaStream.close();
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
    Logger::getInstance().warn() << "Undefined Interface type detected: " << type << std::endl;
    return InterfaceType::UNDEFINED;
}
