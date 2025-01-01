#pragma once

#include <string>
#include <cstdio>
#include <cmath>
#include <cstdio>

#include <curses.h> 
#include <unistd.h>   
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <pugixml.hpp>
#include <json.hpp>
#include <Functions.h>
#include <Global.h>

#define STARTUP_FILE "../configs.json"
#define MODE_KEY "commands"

using json = nlohmann::json;

/**
 * @struct Mode
 * @brief Represents various operational modes of the terminal with corresponding command-line prompts.
 */
struct Mode 
{
    // Various modes and their corresponding command-line prompts.
    std::string userExec = ">";                         ///< User EXEC mode prompt.
    std::string privilegedExec = "#";                   ///< Privileged EXEC mode prompt.
    std::string globalConfiguration = "(config)#";      ///< Global Configuration mode prompt.

    // Netflow related prompts
    std::string flowExporter = "(config-flow-exporter)#";   ///< Flow Exporter Configuration mode prompt.
    std::string flowMoniter = "(config-flow-moniter)#";     ///< Flow Monitor Configuration mode prompt.
    std::string flowRecord = "(config-flow-record)#";       ///< Flow Record Configuration mode prompt.

    // Interface-related prompts.
    std::string dialer = "(config-if)#";                ///< Dialier Interface Configuration mode prompt.
    std::string ethernet = "(config-if)#";              ///< Ethernet Interface Configuration mode prompt.
    std::string fastEthernet = "(config-if)#";          ///< Fast Ethernet Interface Configuration mode prompt.
    std::string gigabitEthernet = "(config-if)#";       ///< Gigabit Ethernet Interface Configuration mode prompt.
    std::string loopback = "(config-if)#";              ///< Loopback Interface Configuration mode prompt.
    std::string portchannel = "(config-if)#";           ///< Port-Channel Interface Configuration mode prompt.
    std::string tunnel = "(config-if)#";                ///< Tunnel Interface Configuration mode prompt.
    std::string virtualTemplate = "(config-if)#";       ///< Virtual-Template Interface Configuration mode prompt.
    std::string vlan = "(config-if)#";                  ///< VLAN Interface Configuration mode prompt.

    // Policy based routing
    std::string classMap = "(config-cmap)#";            ///< Class Map mode prompt.
    std::string dhcp = "(config-dhcp)#";                ///< DHCP Configuration mode prompt.
    std::string extendedACL = "(config-ext-nacl)";      ///< Extended ACL Configuration mode prompt.
    std::string standardACL = "(config-std-nacl)#";     ///< Standard ACL Configuration mode prompt.
    std::string policyMap = "(config-pmap)#";           ///< Policy Map Configuration mode prompt.

    // Routing protocol prompts
    std::string bgp = "(config-router)#";               ///< BGP Routing Configuration mode prompt.
    std::string eigrp_classic = "(config-router)#";     ///< EIGRP Classic Routing Configuration mode prompt.
    std::string eigrp_named = "(config-router)#";       ///< EIGRP Named Routing COnfiguration mode prompt.
    std::string ospf = "(config-router)#";              ///< OSPF Routing Configuration mode prompt.
    std::string rip = "(config-router)#";               ///< RIP Routing Configuration mode prompt.
};

/**
 * @struct MacList
 * @brief Holds lists of MAC addresses categorized by interface type.
 */
struct MacList 
{
    std::vector<std::string> Ethernet;          ///< List of Ethernet MAC addresses.
    std::vector<std::string> FastEthernet;      ///< List of Fast Ethernet MAC Addresses.
    std::vector<std::string> GigabitEthernet;   ///< List of Gigabit Ethernet MAC Addresses.
};

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
};

/**
 * @class volatileValueUsage
 * @brief Manages usage of volatile values within configurations.
 *
 * Provides methods to retreive string representations of various volatile parameters.
 */
class volitileValueUsage 
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

    int value = 0;      ///< Integer value
    int ip = 0;         ///< IP address
    int ipv6 = 0;       ///< IPv6 address
    int subnet = 0;     ///< Subnet mask
    int mac = 0;        ///< MAC address
    int xyz = 0;        ///< XYZ coordinate
    int id = 0;         ///< Identifier
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

    /**
     * @brief Constructor for the Configs class.
     *
     * Initializes the Configs object without any parameters.
     */
    Configs(); 

    /**
     * @brief Initializes configuration settings from a startup file.
     *
     * Loads JSON configuration data from the specified startup file, processes interface configurations,
     * and sets up MAC address lists. If the startup file is empty or cannot be opened, initializes with default settings.
     *
     * @param startupFilename The path to the startup JSON configuration file. Defaults to STARTUP_FILE.
     */
    void initConfigs(const std::string& startupFilename = STARTUP_FILE);

    /**
     * @brief Recovers configuration commands from the loaded JSON data.
     *
     * Processes the JSON configuration tree and retrieves a list of commands for recovery purposes.
     *
     * @return std::vector<std::string> A vector containing the recovered command strings.
     */
    std::vector<std::string> recoverConfigs(); 

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
     * @param changeMode Boolean flag indicating whether the command triggers a mode change.
     * @param exitMode Boolean flag indicating whether the command triggers an exit from the current mode.
     * @param isList Boolean flag indicating if the command should be listed.
     */
    void saveCommand(std::vector<std::string>& oldCommand, std::vector<std::string>& command, bool changeMode, bool exitMode, bool& isList); 

     /**
     * @brief Inserts commands into the configuration tree in the correct order.
     *
     * Ensures that commands are inserted into the JSON configuration tree following the predefined schema order.
     *
     * @param parentNode Pointer to the parent JSON node where the command should be inserted.
     * @param mainCommand The main command string.
     * @param subCommand The sub-command string. Defaults to an empty string.
     * @param isListed Boolean flag indicating if the command is listed. Defaults to false.
     */
    void insertOrdered(nlohmann::ordered_json* parentNode, const std::string& mainCommand, const std::string& subCommand = "", bool isListed = false);

    /**
     * @brief Saves the entire configuration to the startup file.
     *
     * Writes the current JSON configuration tree to the specified startup file in a formatted manner.
     */
    void saveConfig();

    /**
     * @brief Deletes a specific configuration from the JSON tree.
     *
     * Removes the specified configuration command from the JSON structure based on the provided parameters.
     *
     * @param obj Reference to the JSON object from which the configuration should be deleted.
     * @param oldCommand A vector of strings representing the previous command.
     * @param command A vector of strings representing the current command to delete.
     * @param isList Boolean flag indicating if the command is listed.
     */
    void deleteConfig(nlohmann::ordered_json& obj, std::vector<std::string>& oldCommand, std::vector<std::string>& command, bool isList);

    /**
     * @brief Returns the current JSON node to the root of the XML document.
     *
     * Traverses up the JSON tree until the specified root node is reached.
     *
     * @param node Reference to the current XML node.
     * @param root Reference to the root XML node.
     */
    void returnToRoot(pugi::xml_node& node, pugi::xml_node& root);

    /**
     * @brief Determines if a given command string is volatile.
     *
     * Checks if the provided command string matches any known volatile parameters or follows specific volatile formats.
     *
     * @param str The command string to evaluate.
     * @return true If the command is volatile; otherwise, false.
     */
    bool isVolitile(const std::string& str);

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
     * @param command The type of volatile parameter (e.g., "WORD", "IP").
     * @param com The actual command string entered by the user.
     * @param currentJson The current JSON node being processed.
     * @return std::string The resolved key for the volatile parameter.
     */
    std::string getVolitileValue(std::string& command, std::string& com, nlohmann::ordered_json currentJson);

    /**
     * @brief Overloaded method to retrieve the value of a volatile parameter based on the command.
     *
     * Similar to the above method but uses a list of existing volatile values to determine the appropriate key.
     *
     * @param command The type of volatile parameter (e.g., "WORD", "IP").
     * @param com The actual command string entered by the user.
     * @param volatileValues A vector of strings representing existing volatile values.
     * @return std::string The resolved key for the volatile parameter.
     */
    std::string getVolitileValue(std::string& command, std::string com, std::vector<std::string> volitileValues);

    /**
     * @brief Helper method to resolve the key for a volatile parameter.
     *
     * Determines the base key name based on the command type.
     *
     * @param command The type of volatile parameter (e.g., "WORD", "IP").
     * @param com The actual command string entered by the user.
     * @return std::string The base key name for the volatile parameter.
     */
    std::string getVolitileValueHelper(std::string& command, std::string& com);

    /**
     * @brief Updates the global configuration history from the local history.
     *
     * Clears the previous global configuration and appends the current root node.
     */
    void historyToGlobal();

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
    
    Mode mode;                          ///< Contains various operationsl modes and their prompts.
    std::string currentMode;            ///< Indicates the current operational mode.
    std::string prevMode;               ///< Stores the previous operational mode
    std::string startupFileName{};      ///< Path to the startup configuration file

    MacList macAddressList;             ///< Categorized list of MAC addresses by interface type.
    std::string OUI;                    ///< Organizationally Unique Identifier for MAC addresses.
    bool no = false;                    ///< Flag indicating negation of a command.
    bool configMode = true;             ///< Flag indicating if the terminal is in configuration mode.

    nlohmann::ordered_json root;                        ///< Root of the JSON configuration tree.
    nlohmann::ordered_json* configNode = &root;         ///< Pointer to the current configuration node.

    nlohmann::ordered_json configSchema;                ///< Schema defining the configuration structure
    nlohmann::ordered_json* modeSchema;                 ///< Pointer to the current mode's schema
    nlohmann::ordered_json* tempModeSchema;             ///< Temporary pointer for schema operations.

    std::vector<std::string> physicalInterfaces;        ///< List of physical interface names.
    std::vector<nlohmann::ordered_json*> modeHistory;   ///< History of configuration nodes for mode management
	
private:

    std::vector<std::string> volitileInputs{"WORD", "LINE", "A.B.C.D", "X:X:X:X::X", "X:X:X:X::X/<0-128>", "H.H.H", "x/y/z"}; ///< List of volatile input types
    std::vector<std::string> inputs{"ip", "subnet", "id", "value", "ipv6", "mac"}; ///< List of inputs parameter names.

    nlohmann::ordered_json *prevConfig;     ///< Pointer to the previous configuration node
    std::vector<std::string> recover;       ///< List of commands recovered from the configuration.
};
