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

struct Mode {
    // Various modes and their corresponding command-line prompts.
    std::string userExec = ">";
    std::string privilegedExec = "#";
    std::string globalConfiguration = "(config)#";
    std::string classMap = "(config-cmap)#";
    std::string dhcp = "(config-dhcp)#";
    std::string extendedACL = "(config-ext-nacl)";
    std::string flowExporter = "(config-flow-exporter)#";
    std::string flowMoniter = "(config-flow-moniter)#";
    std::string flowRecord = "(config-flow-record)#";

    // Interface-related prompts.
    std::string dialer = "(config-if)#";
    std::string ethernet = "(config-if)#";  
    std::string fastEthernet = "(config-if)#";
    std::string gigabitEthernet = "(config-if)#";
    std::string loopback = "(config-if)#";
    std::string portchannel = "(config-if)#";
    std::string tunnel = "(config-if)#";
    std::string virtualTemplate = "(config-if)#";
    std::string vlan = "(config-if)#";  
    std::string policyMap = "(config-pmap)#"; 
    std::string bgp = "(config-router)#";
    std::string eigrp_classic = "(config-router)#";
    std::string eigrp_named = "(config-router)#";
    std::string ospf = "(config-router)#";
    std::string rip = "(config-router)#";
    std::string standardACL = "(config-std-nacl)#";
};

struct macList {
    // List of Ethernet MAC addresses.
    std::vector<std::string> Ethernet;
    // List of FastEthernet MAC addresses.
    std::vector<std::string> FastEthernet; 
    // List of GigabitEthernet MAC addresses.
    std::vector<std::string> GigabitEthernet; 
};

struct com {
    // Name of the communication object.
    std::string name;   
    // Description of the communication object.
    std::string description; 
};

class volitileValueUsage {
public:
    // Returns 'value' as a std::string.
    std::string getValue() { return std::to_string(value); } 
    // Returns 'ip' as a std::string.
    std::string getIp() { return std::to_string(ip); } 
    // Returns 'ipv6' as a std::string.
    std::string getIpv6() { return std::to_string(ipv6); } 
    // Returns 'subnet' as a std::string.
    std::string getSubnet() { return std::to_string(subnet); } 
    // Returns 'mac' as a std::string.
    std::string getMac() { return std::to_string(mac); } 
    // Returns 'xyz' as a std::string.
    std::string getXYZ() { return std::to_string(xyz); } 
    // Returns 'id' as a std::string.
    std::string getID() { return std::to_string(id); } 
private:
    // Integer value.
    int value = 0;   
    // IP address.
    int ip = 0; 
    // IPv6 address.
    int ipv6 = 0; 
    // Subnet mask.
    int subnet = 0;
    // MAC address.
    int mac = 0; 
    // XYZ parameter.
    int xyz = 0; 
    // Identifier.
    int id = 0; 
};

class Configs {
public:

    // Constructor for the Configs class.
    Configs(); 
    void initConfigs(const std::string& startupFilename = STARTUP_FILE);
    // Recovers JSON data as a vector of std::strings.
    std::vector<std::string> recoverConfigs(); 
    // Processes a JSON for recovery commands
    void processConfigs(nlohmann::ordered_json* currentNode, std::vector<std::string> command, std::vector<std::string>& commandList);
    // Saves commands and updates mode.
    void saveCommand(std::vector<std::string>& oldCommand, std::vector<std::string>& command, bool changeMode, bool exitMode, bool& isList); 
    // Inserts items in the correct order
    void insertOrdered(nlohmann::ordered_json* parentNode, const std::string& mainCommand, const std::string& subCommand = "", bool isListed = false);
    // Saves the full configuration
    void saveConfig();
    // Deletes an configuration
    void deleteConfig(nlohmann::ordered_json& obj, std::vector<std::string>& oldCommand, std::vector<std::string>& command, bool isList);
    // Returns to the root of the XML document.
    void returnToRoot(pugi::xml_node& node, pugi::xml_node& root);
    // Checks if a std::string represents a volatile parameter.
    bool isVolitile(const std::string& str);
    // Combines a vector of commands into a single command
    std::string joinCommand(const std::vector<std::string>& command);
    // Retrieves the value of a volatile parameter based on command.
    std::string getVolitileValue(std::string& command, std::string& com, nlohmann::ordered_json currentJson);
    std::string getVolitileValue(std::string& command, std::string com, std::vector<std::string> volitileValues);
    // Helper for getVolitileValue, does the actual lookup
    std::string getVolitileValueHelper(std::string& command, std::string& com);
    // Updates global history from local history.
    void historyToGlobal();
    // Sets schema mode
    void setSchemaMode(const std::string& mode);
    // Print configs
    void printConfig();

    // Mode settings for command-line prompts.
    Mode mode;
    // Current mode.
    std::string currentMode;
    // Previous mode.
    std::string prevMode;
    // Startup file name
    std::string startupFileName{};

    // Config root
    nlohmann::ordered_json root;
    // JSON node representing the configuration.
    nlohmann::ordered_json* configNode = &root;

    // Config Schema for config
    nlohmann::ordered_json configSchema;
    // Config schema for specific node
    nlohmann::ordered_json* modeSchema;
    // Temp config schema
    nlohmann::ordered_json* tempModeSchema;

    // List of physical interfaces.
    std::vector<std::string> physicalInterfaces;
    // History of JSON nodes for different modes.
    std::vector<nlohmann::ordered_json*> modeHistory;

    // List of MAC addresses categorized by type.
    macList macAddressList;
    // Organizationally Unique Identifier.
    std::string OUI;
    // Boolean flag for additional logic.
    bool no = false;
    // Boolean flag for when in config mode
    bool configMode = true;
	
private:

    // List of volatile input types.
    std::vector<std::string> volitileInputs{"WORD", "LINE", "A.B.C.D", "X:X:X:X::X", "X:X:X:X::X/<0-128>", "H.H.H", "x/y/z"};
    // Previous configuration JSON node.
    nlohmann::ordered_json *prevConfig;
    // List of input parameters
    std::vector<std::string> inputs{"ip", "subnet", "id", "value", "ipv6", "mac"};
    // List of recoverable items.
    std::vector<std::string> recover;
};
