#pragma once

#include <Time.h>
#include "Interface.h"
#include <Eigrp.h>
#include <Ospf.h>
#include <Rip.h>
#include <Bgp.h>

#include <Functions.h>
#include "Console.h"

using namespace std;

// Enum for representing different routing modes
enum RoutingMode
{
    BGP,
    EIGRP_CLASSIC,
    EIGRP_NAMED,
    OSPF,
    RIP
};

// Terminal class interits from COnsole to simulate a terminal interface for the virtual router
class Terminal : public Console {
public:
    com errorCommand, carriageReturnCommand;

    // Constructor
    // Initialized the Terminal with optional debug mode for a packet capture
    Terminal(bool enableDebug = false);

    // Captures and processes user input
    void handleInput();
	
private:

    Variable variable;

    // Processes a single command
    void executeCommand(string& command);
    // Reverses the effect of process command (e.g., for "no" commands)
    void undoCommand(string& command);
    // Normalized and fixes user-entered commands
    string normalizeCommand(const string& command);
    // Retreived a list of possible commands based on the current directory and input
    vector<com> GetAvailableCommands(const nlohmann::json& commandTree, const string& userInput, bool inPriviledgedMode);
    // Checks if a command belongs to the global command set
    bool isGlobalCommand(string& commandName);
    // Prints available commands 
    void displayAvailableCommands(vector<com> commandList);
    // Extracts the last word from an input string
    string getLastWord(const std::string& input);
    // Splits a string into individual words, preservind certain charecters
    vector<string> splitIntoWords(const std::string& str);
    // Formats a string by trimming leasing and trailing spaces
    string trimString(string str);
    // Matches a user input against a specific pattern (e.g., IPv6, MAC address)
    bool matchInputPattern(const std::string& userInput, const std::string& expectedPattern);
    // Checks if a string represents a valid number
    bool isNumeric(const std::string& input);
    // Validates if a JSON object represents a valid command directory
    bool isValidCommandDirectory(nlohmann::json& directory);
    // Handles pagination for long command lists
    bool handlePagination(int& lineCount);
    // Switches the terminal to a new operational mode
    void changeMode(string& newMode);
    // Pads a string with leading zeros (e.g., for IPv6 segments)
    std::string padWithZeros(const std::string& input);
    // Expands an abbreviated IPv6 address to its full form
    std::string expandIPv6Address(const std::string& ipv6Address);
    // Splits a string into tokens based on a delimiter
    std::vector<std::string> tokenize(const std::string& input, char delimiter);
    // Validates if a string is a valid IPv6 address
    bool isIPv6Address(const std::string& address);
    // Validates if a string is a valid IPv6 address with a subnet mask
    bool isIPv6AddressWithMask(const std::string& addressWithMask);
    // Validates if a string is a valid MAC address
    bool isMACAddress(const std::string& macAddress);
    // Recovers the terminal state from saved configurations
    void recoverState();

    // Router Modes
    void configureInterfaceMode(string& type);
    void configureRoutingMode(RoutingMode type);

    // threads
    void runDhcp();
    void runEigrp();
    void runOspf();
    void runBgp();
    void runRip();

    // Member variables
    unsigned long interfaceID;		// Unique identifier for interfaces
    int routingProtocolID;		// ID of the current routing protocol
	
    map<int, std::shared_ptr<Interface>>* activeInterfaces; // pointer to a map of active interfaces

    vector<string> globalCommandList{"exit", "end", "?", "vk_tab"}; // List of global commands
    vector<string> commandHistory; // History of previous entered commands

    DoTime timeManager; // Manages time-related functionality

    string currentPattern;		// Current matching pattern
    string endCommandString;		// String for marking the end of a command
    string previousMatch;		// Previous successfull command match
    string currentCommand;		// Current command being processed
    string currentSubMode;		// Current sub-mode (e.g., specific interface or protocol)

    nlohmann::json commandTree;		// JSON structure holding the command hierarchy
    nlohmann::json currentDirectory;	// Current directory in the command tree
    nlohmann::json workingDirectory;    // Working directory in the JSON structure

    vector<string> executionHistory;	// History of executed commands

    bool isRunning = true;		// Terminal run state
    bool endOfCommand = false;		// Indicates if the command has reached its end
    bool isNextWordHelpRequested = false; // Indicated if help is requested for the next word
    bool isMatchSuccessful = false;     // Indicates if a command match was successfull
    bool isLineBasedInput = false;	// indicates if input is line-based
    bool isHelpModeActive = false;	// Indicates if help mode is active
    bool isCommandValid = false;	// Indicates if the command is valid
    bool isPatternMatching = false;	// Indicates if the input matches a pattern
    bool isPatternMatchEnd = false;	// Indicates the end of a matching pattern
    bool isModeChanged = false;		// Indicates if the operational mode has changed
    bool isCommandExecutionSuccessful = false; // Indicates if the command was successful
    bool isGlobalCommandExecution = false; // Indicates if a global command is being executed

    condition_variable stateCondition; // Condition variabel for thread synchronization

    bool isDebugModeEnabled; // Debug mode flag
};
