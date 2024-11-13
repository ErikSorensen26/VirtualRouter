#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <regex>
#include <cstdio>
#include <cmath>
#include <cstdio>
#include <memory>

#ifdef _WIN32
	#include <conio.h>
	#include <windows.h>
#else
	#include <curses.h>
	#include <unistd.h>
	#include <termios.h>
	#include <sys/ioctl.h>
	#include <fcntl.h>
#endif

#include <pugixml.hpp>
#include <json.hpp>
#include <Functions.h>
#include <Global.h>

using json = nlohmann::json;
using namespace std;

struct Mode {
	// Various modes and their corresponding command-line prompts.
	string userExec = ">";
	string privilegedExec = "#";
	string globalConfiguration = "(config)#";
	string classMap = "(config-cmap)#";
	string dhcp = "(config-dhcp)#";
	string extendedACL = "(config-ext-nacl)";
	string flowExporter = "(config-flow-exporter)#";
	string flowMoniter = "(config-flow-moniter)#";
	string flowRecord = "(config-flow-record)#";

	// Interface-related prompts.
	string dialer = "(config-if)#";
	string ethernet = "(config-if)#";
	string fastEthernet = "(config-if)#";
	string gigabitEthernet = "(config-if)#";
	string loopback = "(config-if)#";
	string portchannel = "(config-if)#";
	string tunnel = "(config-if)#";
	string virtualTemplate = "(config-if)#";
	string vlan = "(config-if)#";
	string policyMap = "(config-pmap)#";
	string bgp = "(config-router)#";
	string eigrp = "(config-router)#";
	string ospf = "(config-router)#";
	string rip = "(config-router)#";
	string standardACL = "(config-std-nacl)#";
};

struct macList {
	// List of Ethernet MAC addresses.
	vector<string> Ethernet;
	// List of FastEthernet MAC addresses.
	vector<string> FastEthernet; 
	// List of GigabitEthernet MAC addresses.
	vector<string> GigabitEthernet; 
};

struct com {
	// Name of the communication object.
	string name; 
	// Description of the communication object.
	string description; 
};

class volitileValueUsage {
public:
	// Returns 'value' as a string.
	string getValue() { return std::to_string(value); } 
	// Returns 'ip' as a string.
	string getIp() { return std::to_string(ip); } 
	// Returns 'ipv6' as a string.
	string getIpv6() { return std::to_string(ipv6); } 
	// Returns 'subnet' as a string.
	string getSubnet() { return std::to_string(subnet); } 
	// Returns 'mac' as a string.
	string getMac() { return std::to_string(mac); } 
	// Returns 'xyz' as a string.
	string getXYZ() { return std::to_string(xyz); } 
	// Returns 'id' as a string.
	string getID() { return std::to_string(id); } 
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
	void initConfigs();
	// Recovers XML data as a vector of strings.
    vector<string> recoverXml(); 
	// Processes an XML node based on a command.
    void processNode(const pugi::xml_node& node, std::string command); 
	// Saves commands and updates mode.
	void saveCommand(vector<string>& oldCommand, vector<string>& command, bool& changeMode, bool& isListed); 
	// Traverses and processes XML nodes.
	bool TravelNode(vector<string>& command, vector<string> oldCommand, pugi::xml_node& config_node, pugi::xml_node& save, bool& changeMode, bool& isList, int offset); 
	// Deletes an XML node and its children.
	bool deleteNodeAndAllChildren(pugi::xml_node& node, pugi::xml_node& save); 
	// Returns to the root of the XML document.
	void ReturnToRoot(pugi::xml_node& node, pugi::xml_node& root);
	// Checks if a string represents a volatile parameter.
	bool isVolitile(string& str);
	// Retrieves the value of a volatile parameter based on command.
	string getVolitileValue(string& command, string& com);
	// Updates global history from local history.
	void historyToGlobal();

	// Mode settings for command-line prompts.
	Mode mode;
	// Current mode.
    string currentMode;
	// Previous mode.
	string prevMode;

	// PugiXML document object.
	pugi::xml_document doc;
	// XML node representing the configuration.
    pugi::xml_node config_node;

	// List of physical interfaces.
	vector<string> physicalInterfaces;

	// List of MAC addresses categorized by type.
	macList macAddressList;
	// Organizationally Unique Identifier.
	string OUI;
	// Boolean flag for additional logic.
	bool no = false;
	
private:
	// Boolean flag indicating no more volatile parameters.
	bool noMoreVol = false;
	// Boolean flag for additional child processing.
    bool moreChild = false;
	// Boolean flag indicating if in configuration mode.
	bool configMode;
	// Boolean flag for clearing settings.
	bool clear = false;
	// Command for non-volatile settings.
	string nonVolCommand;
	// Current command for non-volatile settings.
	string curNonVolCommand;

	// List of volatile input types.
	vector<string> volitileInputs{"WORD", "LINE", "A.B.C.D", "X:X:X:X::X", "X:X:X:X::X/<0-128>", "H.H.H", "x/y/z"};

	// History of XML nodes for different modes.
	vector<pugi::xml_node> modeHistory;

	// Depth level for XML traversal.
	int level = 0;

	// Placeholder string for testing.
	string test;

	// Previous configuration XML node.
	pugi::xml_node prevConfig;

	// List of input parameters
    vector<string> inputs{"ip", "subnet", "id", "value", "ipv6", "mac"};
	// List of recoverable items.
    vector<string> recover;
};