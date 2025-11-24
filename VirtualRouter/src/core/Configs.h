// Configs.h

#ifndef CONFIGS_H
#define CONFIGS_H

#include <string>
#include <cstdio>
#include <cmath>
#include <cstdio>

#include <curses.h> 
#include <unistd.h>   
#include <termios.h>
#include <fstream>

#include <pugixml.hpp>
#include <json.hpp>
#include <Functions.h>

#define COMMAND_TREE "./configs/Commands.json"
#define CONFIG_SCHEMA "./configs/ConfigSchema.json"
#define HW_CONFIG_FILE "./configs/Configs.json"
#define ROUTER_CONFIG_FILE "./dir/configs.json"
#define MODE_KEY "commands"

/**
 * @struct StartupFiles
 */
struct StartupFiles
{
    std::string startupFile = ROUTER_CONFIG_FILE;
    std::string routerConfigFile = ROUTER_CONFIG_FILE;
    std::string hwConfigFile = HW_CONFIG_FILE;
};

using json = nlohmann::json;

class Global;
struct ModeConfig;
class HardwareManager;

/**
 * @struct Com
 * @brief Represents a command with a name and description
 */
struct Com 
{
    // Name of the communication object.
    std::string name;   
    // Description of the communication object.
    std::string description; 
    // Properties
    std::vector<std::string> properties;

    // Support
    enum class Support 
    {
        SUPPORTED,
        PARTIAL,
        NO_SUPPORT
    };
    Support support = Support::NO_SUPPORT;
};

/**
 * @class volatileValueUsage
 * @brief Manages usage of volatile values within configurations.
 *
 * Provides methods to retreive string representations of various volatile parameters.
 */
class volatileValueUsage 
{
public:

    /**
     * @brief Retrieves the string representation of the value parameter.
     * @return std::string The string representation of value.
     */
    std::string getValue() { return std::to_string(value); } 

    /**
     * @brief Retrieves the string representation of the IP parameter.
     * @return std::string The string representation of IP.
     */
    std::string getIp() { return std::to_string(ip); } 

    /**
     * @brief Retrieves the string representation of the IPv6 parameter.
     * @return std::string The string representation of IPv6.
     */
    std::string getIpv6() { return std::to_string(ipv6); } 

    /**
     * @brief Retrieves the string representation of the subnet parameter.
     * @return std::string The string representation of subnet.
     */
    std::string getSubnet() { return std::to_string(subnet); } 

    /**
     * @brief Retrieves the string representation of the MAC parameter.
     * @return std::string The string representation of MAC.
     */
    std::string getMac() { return std::to_string(mac); } 

    /**
     * @brief Retrieves the string representation of the XYZ parameter.
     * @return std::string The string representation of XYZ.
     */
    std::string getXYZ() { return std::to_string(xyz); } 

    /**
     * @brief Retrieves the string representation of the ID parameter.
     * @return std::string The string representation of ID.
     */
    std::string getID() { return std::to_string(id); } 

private:

    unsigned int value = 0;      ///< Integer value
    unsigned int ip = 0;         ///< IP address
    unsigned int ipv6 = 0;       ///< IPv6 address
    unsigned int subnet = 0;     ///< Subnet mask
    unsigned int mac = 0;        ///< MAC address
    unsigned int xyz = 0;        ///< XYZ coordinate
    unsigned int id = 0;         ///< Identifier
};

class IFileSystem
{
public:
    virtual ~IFileSystem() = default; 
    virtual bool readFile(const std::string& path, std::string& content) = 0; 
    virtual bool writeFile(const std::string& path, const std::string& content) = 0;
    virtual bool fileExists(const std::string& path) = 0;
    virtual void removeFile(const std::string& path) = 0;
};

class FileSystem : public IFileSystem
{
public:
    virtual ~FileSystem() override = default;
    bool readFile(const std::string& path, std::string& content) override
    {
        std::ifstream file(path, std::ios::in);
        if (!file.is_open())
        {
            return false; // File not found or cannot be opened
        }

        // Read file content
        content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        return true;
    }

    bool writeFile(const std::string& path, const std::string& content) override
    {
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file.is_open())
        {
            return false; // Cannot open the file for writing
        }

        file << content;
        file.close();
        return true;
    }

    bool fileExists(const std::string& path) override
    {
        return std::filesystem::exists(path);
    }

    void removeFile(const std::string& path) override
    {

    }
};

/**
 * @class Configs
 * @brief Manages configuration settings for the terminal.
 *
 * The Configs class handles initialization, loading, saving, and processing of configuration data.
 * It interacts with JSON files to manage command hierarchies and interface configurations.
 */
class Configs 
{
public:
    friend class Internal_ConfigTest;

    /**
     * @brief Constructor for the Configs class.
     *
     * Initializes the Configs object without any parameters.
     *
     * @param global Global router system.
     * @param fileSystem Smart pointer to the kind of file system being used.
     */
    Configs(IFileSystem* fileSystem = new FileSystem);

    /**
     * @brief Initializes configuration settings from a startup file.
     *
     * Loads JSON configuration data from the specified startup file, processes interface configurations,
     * and sets up MAC address lists. If the startup file is empty or cannot be opened, initializes with default settings.
     *
     * @param stfs Struct holding all startup config file information.
     */
    void initConfigs(const StartupFiles& stfs, bool enableDummies = true);

    /**
     * @brief Recovers configuration commands from the loaded JSON data.
     *
     * Processes the JSON configuration tree and retrieves a list of commands for recovery purposes.
     *
     * @param nlohmann::ordered_json optional json for recovering custom configurations.
     * @return std::vector<std::string> A vector containing the recovered command strings.
     */
    std::vector<std::string> recoverConfigs(nlohmann::ordered_json* json = nullptr);

    /**
     * @brief Processes JSON data to extract configuration commands.
     *
     * Recursively traverses the JSON configuration tree to build a list of executable commands based on the provided structure.
     *
     * @param currentNode Pointer to the current JSON node being processed.
     * @param command A vector of strings representing the current command path.
     * @param commandList A reference to a vector that accumulates the processed command strings.
     */
    void processConfigs(nlohmann::ordered_json* currentNode, std::vector<std::string> command, std::vector<std::string>& commandList);

    /**
     * @brief Saves a command and updates the mode accordingly.
     *
     * Appends the given command to the configuration tree, handling mode changes and ensuring correct command ordering.
     *
     * @param oldCommand A vector of strings representing the previous command.
     * @param command A vector of strings representing the current command to save.
     * @param modeConfig A struct with mode configs for session.
     * @param changeMode Boolean flag indicating whether the command triggers a mode change.
     * @param exitMode Boolean flag indicating whether the command triggers an exit from the current mode.
     * @param isList Boolean flag indicating if the command should be listed.
     * @return bool Indicating if the save was successful
     */
    bool saveCommand(std::vector<std::string>& oldCommand, std::vector<std::string>& command, ModeConfig& modeConfig, bool changeMode, bool exitMode, bool isList); 

     /**
     * @brief Inserts commands into the configuration tree in the correct order.
     *
     * Ensures that commands are inserted into the JSON configuration tree following the predefined schema order.
     *
     * @param parentNode Pointer to the parent JSON node where the command should be inserted.
     * @param modeConfig A struct with mode configs for session.
     * @param mainCommand The main command string.
     * @param subCommand The sub-command string. Defaults to an empty string.
     * @param isListed Boolean flag indicating if the command is listed. Defaults to false.
     */
    void insertOrdered(nlohmann::ordered_json* parentNode, ModeConfig& modeConfig, const std::string& mainCommand, const std::string& subCommand = "", bool isListed = false);

    /**
     * @brief Saves the entire configuration to the startup file.
     *
     * Writes the current JSON configuration tree to the specified startup file in a formatted manner.
     *
     * @return boolean Indicating if the save was successful.
     */
    bool saveConfig();

    /**
     * @brief Deletes a specific configuration from the JSON tree.
     *
     * Removes the specified configuration command from the JSON structure based on the provided parameters.
     *
     * @param modeConfig Reference to the mode configs with config objects
     * @param oldCommand A vector of strings representing the previous command.
     * @param command A vector of strings representing the current command to delete.
     * @param isList Boolean flag indicating if the command is listed.
     */
    bool deleteConfig(ModeConfig& modeConfig, std::vector<std::string>& oldCommand, std::vector<std::string>& command, bool isList);

    /**
     * @brief Determines if a given command string is volatile.
     *
     * Checks if the provided command string matches any known volatile parameters or follows specific volatile formats.
     *
     * @param str The command string to evaluate.
     * @return true If the command is volatile; otherwise, false.
     */
    bool isVolatile(const std::string& str);

    /**
     * @brief Combines a vector of command strings into a single command string.
     *
     * Concatenates the individual command strings with spaces to form a complete command.
     *
     * @param command A vector of strings representing individual command parts.
     * @return std::string The combined command string.
     */
    std::string joinCommand(const std::vector<std::string>& command);

    /**
     * @brief Retrieves the value of a volatile parameter based on the command.
     *
     * Determines the appropriate key for a volatile parameter and appends an index if necessary to ensure uniqueness.
     *
     * @param type The type of volatile parameter (e.g., "WORD", "IP").
     * @param value The actual command string entered by the user.
     * @param currentJson The current JSON node being processed.
     * @return std::string The resolved key for the volatile parameter.
     */
    std::string getVolatileValue(std::string& type, std::string& value, nlohmann::ordered_json currentJson);

    /**
     * @brief Overloaded method to retrieve the value of a volatile parameter based on the command.
     *
     * Similar to the above method but uses a list of existing volatile values to determine the appropriate key.
     *
     * @param type The type of volatile parameter (e.g., "WORD", "IP").
     * @param value The actual command string entered by the user.
     * @param volatileValues A vector of strings representing existing volatile values.
     * @return std::string The resolved key for the volatile parameter.
     */
    std::string getVolatileValue(std::string& type, std::string value, std::vector<std::string> volatileValues);

    /**
     * @brief Helper method to resolve the key for a volatile parameter.
     *
     * Determines the base key name based on the command type.
     *
     * @param type The type of volatile parameter (e.g., "WORD", "IP").
     * @param value The actual command string entered by the user.
     * @return std::string The base key name for the volatile parameter.
     */
    std::string getVolatileValueHelper(std::string& command, std::string& com);

    /**
     * @brief Sets the schema mode for setting configurations
     *
     * Sets the schema mode for setting configurations in order to pre-set the command order in the configurations file.
     *
     * @param mode The mode that is being switched to.
     */
    void setSchemaMode(const std::string& mode);

    /**
     * @brief Prints the current JSON configuration to the console.
     *
     * Outputs the entire JSON configuration tree in a formatted manner for debugging or verification purposes.
     */
    void printConfig();

    // Public member variables

    std::string routerConfigFilename{};     ///< Path to the startup configuration file
    
    nlohmann::ordered_json root;                        ///< Root of the JSON configuration tree.
    nlohmann::ordered_json configSchema;                ///< Schema defining the configuration structure

    IFileSystem* fileSystem; ///< File system interface.

    Global* global = nullptr;
    HardwareManager* hwManager = nullptr;
	
private:

    std::vector<std::string> volatileInputs{"WORD", "LINE", "A.B.C.D", "X:X:X:X::X", "X:X:X:X::X/<0-128>", "H.H.H", "x/y/z"}; ///< List of volatile input types
    std::vector<std::string> inputs{"ip", "subnet", "id", "value", "ipv6", "mac"}; ///< List of inputs parameter names.

    std::vector<std::string> recover;       ///< List of commands recovered from the configuration.
};

#endif // CONFIGS_H
