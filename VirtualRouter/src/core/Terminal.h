// Terminal.h

#ifndef TERMINAL_H
#define TERMINAL_H

#include <Time.h>
#include "Interface.h"
#include <Eigrp.h>
#include <Ospf.h>
#include <Rip.h>
#include <Bgp.h>
#include <Functions.h>
#include "Console.h"

/**
 * @enum RoutingMode
 * @brief Enumerates the various routing protocols supported by the Terminal.
 */
enum RoutingMode
{
    BGP,            ///< Border Gateway Protocol
    EIGRP_CLASSIC,  ///< Enhanced Interior Gateway Routing Protocol Classic
    EIGRP_NAMED,    ///< Enhanced Interior Gateway Routing Protocol Named
    OSPF,           ///< Open Shortest Path First
    RIP             ///< Routing Information Protocol
};

/**
 * @class Terminal
 * @brief Represents a command-line terminal interface.
 *
 * The terminal class provides functionalities to interact with the user via the command line.
 * It supports various commands, modes, and maintains the state between sessions.
 */
class Terminal : public Console 
{
public:
    // Friend classes for testing
    friend class TerminalTest;

    Com errorCommand;           ///< Represents an error command
    Com carriageReturnCommand;  ///< Represents a carriage return command

    /**
     * @brief Constructor for the Terminal class.
     * 
     * Initializes the terminal by setting up debugging options, loading command configurations,
     * setting the default mode, and restoring the previous state if available.
     * 
     * @param enableDebug A boolean flag to enable or disable debug mode.
     */
    Terminal(bool enableDebug = false);

    /**
     * @brief Constructor for the Terminal class.
     * 
     * Initializes the terminal by setting up debugging options, loading command configurations,
     * setting the default mode, and restoring the previous state if available.
     * 
     * @param term IConsole shared pointer for testing
     * @param fs IFileSystem shared pointer for testing
     */
    Terminal(std::shared_ptr<IConsole> term, std::shared_ptr<IFileSystem> fs);

    /**
     * @brief Initialization function for the Terminal class
     *
     * This function fully initialized this class by setting the configuration files
     */
    void initTerminal();

    /**
     * @brief Captures and processes user input in the terminal.
     * 
     * This function reads the user's input, processes IPv6 addresses if applicable,
     * handles shortcuts (e.g., Ctrl-Z for mode switching), and executes valid commands.
     *
     * @param input String for giving manual input
     */
    bool handleInput(std::string input = "");
	
private:
    
    /**
     * @brief Executes a single command
     *
     * Processes the given command string and performs the corresponding action within the terminal.
     *
     * @param command The command string to process and execute.
     * @return boolean Indicates whether the execution was a success or not.
     */
    bool executeCommand(std::string& command);

    /**
     * @brief Reverses the effects of a processed command.
     *
     * Specifically used for handling "no" commands that negate or undo previous configurations.
     *
     * @param command The command string to undo
     */
    void undoCommand(std::string& command);

    /**
     * @brief Normalizes and fixes user-entered commands.
     *
     * Processes the input command string to ensure it adheres to expected formats and standards.
     *
     * @param command The original user-entered command string.
     * @return std::string The normalized and formatted command string.
     */
    std::string normalizeCommand(const std::string& command);

    /**
     * @brief Appends a word to the fully formatted and volatile command strings for line-based inputs.
     *
     * Concatenates the current word to both the fully formatted command and the volatile command,
     * maintaining the structure required for line-based command processing.
     *
     * @param parsedWords A vector of string representing the parsed command words.
     * @param currentIndex The current index in the parsedWords vector thats being processed.
     * @param fullyFormattedCommand Reference to the string accumalating the fully formatted command.
     * @param volatileCommand Reference to the string accumalating the volatile command.
     */
    void appendLineBasedCommand(const std::vector<std::string>& parsedWords, size_t currentIndex, std::string& fullyFormattedCommand, std::string& volatileCommand);

    /**
     * @brief
     *
     * Handles the parsing and matching of individual words in a command that's not part of a line-based input.
     * This includes managing help requests, tab completions, and validating command directories from the command structure JSON.
     *
     * @param word The current word being processed
     * @param previousCommandList A reference to a vector storing previously matched commands.
     * @param formattedOldCommand Reference to the string storing the formatted directory based command.
     * @param fullyFormattedCommand Reference to the string accumulating the fully formatted command.
     * @param volatileCommand Reference to the string accumulating the volatile command.
     * @param inputCommand The original user command string.
     * @param isFirstIteration A flag indicating if this is the first iteration of processing.
     */
    void processNonLineBasedWord(std::string& word, std::vector<Com>& previousCommandList, std::string& formattedOldCommand, std::string& fullyFormattedCommand, std::string& volatileCommand, const std::string& inputCommand, bool& isFirstIteration);

    /**
     * @brief Initializes the processing state for command execution
     *
     * Sets up the initial state variables required for processing user commands,
     * including the current directory, flags for help requests, and input modes.
     */
    void initializeProcessingState();

    /**
     * @brief Detects triggers for help mode based on parsed command words.
     *
     * Evaluates the parsed words to determine if a help request has been made,
     * such as the presence of "?" or "vk_tab" in the input.
     *
     * @param parsedWords A vector of strings representing the parsed command words.
     * @return true If a help trigger is detected; otherwise, false.
     */
    bool detectHelpTriggers(const std::vector<std::string>& parsedWords);

    /**
     * @brief Determines if the current command is a "do" command
     *
     * Checks whether the parsed words represent a "do" command that should be executed
     * in a different mode, ensuring it does not interfere with mode changes.
     *
     * @param parsedWords A vector of strings representing the parsed command words.
     * @return true If the command is a valid "do" command; otherwise, false.
     */
    bool isDoCommand(const std::vector<std::string>& parsedWords);

    /**
     * @brief Executes a "do" command by switching modes, running the command, and restoring the mode.
     *
     * Handles the execution of commands prefixed with "do" by temporarily switching to privileged mode,
     * executing the command, and the reverting to the original mode and configuration.
     *
     * @param remainingCommand The portion of the command string following the "do" keyword.
     * @return std::string Returns "error" to signify no further processing is needed.
     */
    std::string executeDoCommand(std::string remainingCommand);

    /**
     * @brief Handles help requests triggeredf by the "?" character.
     *
     * If a help request is detected and applicable, displays available commands and updates the next line input.
     *
     * @param word The current word being processed.
     * @param previousCommandList A reference to a vector storing previously matched commands.
     * @param inputCommand The original user input command string.
     * @param formattedOldCommand Reference to the string storing the formatted directory based command.
     * @param fullyFormattedCommand Reference to the string accumulating the fully formatted command.
     * @param volatileCommand Reference to the string accumulating the volatile command.
     * @return true If the help question was handled; otherwise, false.
     */
    bool handleHelpQuestion(const std::string& word, std::vector<Com>& previousCommandList, const std::string& inputCommand, std::string& formattedOldCommand, std::string& fullyFormattedCommand, std::string& volatileCommand);

    /**
     * @brief Handles tab completion triggered by the "vk_tab" input.
     *
     * Manages the auto-completion or suggestion of commands based on the current input state and available commands
     *
     * @param word The current word being processed.
     * @param previousCommandList A reference to a vector storing previously matched commands.
     * @param inputCommand The original user input command string.
     * @param formattedOldCommand Reference to the string storing the formatted directory based command.
     * @param fullyFormattedCommand Reference to the string accumulating the fully formatted command.
     * @param volatileCommand Reference to the string accumulating the volatile command.
     * @return true If the tab completion was handled; otherwise, false.
     */
    bool handleTabCompletion(const std::string& word, std::vector<Com>& previousCommandList, const std::string& inputCommand, std::string& formattedOldCommand, std::string& fullyFormattedCommand, std::string& volatileCommand);

    /**
     * @brief Attempts to execute a global command when in an error state.
     *
     * If the terminal is in an error state and the input command is not a global command or exit,
     * switches to global configuration mode to attempt execution. Restores the previous state if unsuccessful.
     *
     * @param inputCommand The original user input command string.
     * @return true If the global command execution was successful; otherwise, false.
     */
    bool attemptGlobalCommand(const std::string& inputCommand);

    /**
     * @brief Handles the marking of invalid input within the command.
     *
     * Flags the command as invalid, stops further processing, and provides user feedback indicating
     * the location of the invalid input.
     *
     * @param formattedOldCommand The formatted command string up to the point of invalid input.
     */
    void handleInvalidInputMarker(const std::string& formattedOldCommand);

    /**
     * @brief Handles the marking of ambiguous input within the command.
     *
     * Flags the command as ambiguous, stops further processing, and provides user feedback indicating
     * the location of the ambiguous input.
     *
     * @param ambiguousCommand The formatted command string up to the point of invalid input.
     */
    void handleAmbiguousInputMarker(const std::string& ambiguousCommand);

    /**
     * @brief Matches the user input command with available commands.
     *
     * Compares the input command with a list of available commands, updates matching states,
     * and determines the next steps based on the number of matches found.
     *
     * @param inputCommand The original user input command string.
     * @param word The current word being processed.
     * @param availableCommands A vector of available command structures to match against.
     * @param previousCommandList A reference to a vector storing previously matched commands.
     * @param formattedOldCommand Reference to the string storing the formatted old command.
     * @param fullyFormattedCommand Reference to the string accumulating the fully formatted command.
     * @param volatileCommand Reference to the string accumulating the volatile command.
     * @param isCommandDone A reference to a boolean indicating if the command matching is complete.
     */
    void matchCommand(const std::string& inputCommand, const std::string& word, const std::vector<Com>& availableCommands, std::vector<Com>& previousCommandList, std::string& formattedOldCommand, std::string& fullyFormattedCommand, std::string& volatileCommand, bool& isCommandDone);

    /**
     * @brief Retrieves a list of available commands based on the current command tree and user input.
     *
     * Parses the command tree JSON structure to find matching commands for the user's input,
     * handles exact matches, pattern matching, and updates the current directory accordingly.
     *
     * @param commandTree The JSON structure representing the command hierarchy.
     * @param userInput The current word input by the user.
     * @param inPrivilegedMode A boolean indicating if the terminal is in privileged mode.
     * @return std::vector<com> A vector of available commands matching the user input.
     */
    std::vector<Com> getAvailableCommands(const std::string& userInput, bool inPriviledgedMode);

    /**
     * @brief Checks if a given command name is a global command.
     *
     * Iterates through the list of global commands to determine if the provided command name matches any global command.
     *
     * @param commandName The name of the command to check.
     * @return true If the command is a global command; otherwise, false.
     */
    bool isGlobalCommand(std::string& commandName);

    /**
     * @brief Displays a list of available commands to the user.
     *
     * Formats and prints the available commands with their descriptions, handling pagination
     * if the number of commands exceeds the terminal's display capacity.
     *
     * @param commandList A vector of command structures to display.
     */
    void displayAvailableCommands(std::vector<Com> commandList);

    /**
     * @brief Retrieves the last word from an input string.
     *
     * Splits the input string into words and returns the last word found.
     *
     * @param input The input string from which to extract the last word.
     * @return std::string The last word in the input string.
     */
    std::string getLastWord(const std::string& input);

    /**
     * @brief Splits a string into individual words based on whitespace and special characters.
     *
     * Parses the input string, handling spaces, question marks, and tab characters to separate it into words.
     *
     * @param str The input string to split.
     * @return std::vector<std::string> A vector of words extracted from the input string.
     */
    std::vector<std::string> splitIntoWords(const std::string& str);

    /**
     * @brief Trims leading and trailing whitespace characters from a string.
     *
     * Removes all leading and trailing whitespace characters from the provided string, returning the trimmed result.
     *
     * @param str The input string to trim.
     * @return std::string The trimmed string with leading and trailing whitespace removed.
     */
    std::string trimString(std::string str);

    /**
     * @brief Matches the user input against a specific pattern (e.g., IPv6, MAC address).
     *
     * Evaluates the user input to determine if it conforms to predefined patterns such as IP addresses,
     * MAC addresses, numeric ranges, or other specified formats.
     *
     * @param userInput The user's input command string.
     * @param expectedPattern The pattern against which to match the user input.
     * @return true If the user input matches the expected pattern; otherwise, false.
     */
    bool matchInputPattern(const std::string& userInput, const std::string& expectedPattern);

    /**
     * @brief Determines if a given string represents a numeric value.
     *
     * Checks whether the input string consists solely of digits, optionally preceded by a sign.
     *
     * @param input The input string to evaluate.
     * @return true If the input string is numeric; otherwise, false.
     */
    bool isNumeric(const std::string& input);

    /**
     * @brief Validates whether a JSON object represents a valid command directory.
     *
     * Checks if the provided JSON object includes a "subcommands" key, indicating it is a valid command directory.
     *
     * @param directory The JSON object representing a command directory.
     * @return true If the directory contains subcommands; otherwise, false.
     */ 
    bool isValidCommandDirectory(nlohmann::json* directory);

    /**
     * @brief Handles pagination for displaying large lists of commands.
     *
     * If the number of lines exceeds a certain threshold (e.g., 10 lines), prompts the user to continue
     * or quit viewing additional commands.
     *
     * @param lineCount A reference to the current line number being displayed.
     * @return true If pagination continues; otherwise, false to stop displaying more lines.
     */
    bool handlePagination(size_t& lineCount);

    /**
     * @brief Changes the terminal's operational mode.
     *
     * Updates the current mode of the terminal, adjusts the working directory based on the new mode,
     * and manages mode history for potential state restoration.
     *
     * @param newMode A reference to the string representing the new mode to switch to.
     * @param processing A boolean flag indicating whether the mode change is part of command processing. Defaults to false.
     */
    bool changeMode(std::string& newMode, bool processing = false);

    /**
     * @brief Exits the current mode and switches to a new mode.
     *
     * Marks the exit command and changes the mode accordingly.
     *
     * @param newMode The mode to switch to upon exiting the current mode.
     */
    inline void exitMode(std::string& newMode) { isExitCommand = true; changeMode(newMode);}

    /**
     * @brief Configures the terminal's interface mode based on the specified type.
     *
     * Switches the terminal's operational mode to match the given interface type,
     * updates the active interfaces list, and adjusts the working directory accordingly.
     *
     * @param type The string representing the interface type to configure (e.g., "Ethernet", "Loopback").
     */
    void configureInterfaceMode(std::string& type);

    /**
     * @brief Determines the interface type based on a string identifier.
     *
     * Maps a string representing an interface type to its corresponding enum value.
     * Logs a warning if the interface type is undefined.
     *
     * @param type The string identifier of the interface type.
     * @return InterfaceType The corresponding enum value of the interface type.
     */
    InterfaceType getInterfaceType(std::string& type);

    /**
     * @brief Masks the prefix over the original.
     * 
     * Takes the original string and copies the prefix over the beginning of it.
     * 
     * @param prefix Reference to the prefix string to be masked.
     * @param original Original string to by copied over.
     * @return std::string The masked string.
     */
    std::string maskInput(const std::string& prefix, std::string original);

    /**
     * @brief Configures the terminal's routing mode based on the specified routing protocol.
     *
     * Switches the terminal's operational mode to match the given routing protocol (e.g., BGP, OSPF),
     * updates the active routing configurations, and adjusts the working directory accordingly.
     *
     * @param type The string value representing the routing mode to configure.
     * @param type Indicates whether your running classic v6 mode.
     */
    void configureRoutingMode(std::string type, bool classicV6 = false);

    /**
     * @brief Configures the terminal's mode based on a address family.
     *
     * Seitches the terminal's operational mode to match the given address family (e.g. IPv4, IPv6),
     * updates the active address family, and adjusts the working directory accordingly.
     *
     * @param addressFamily AddressFamily enum representing the wanted address family.
     */
    void configureAddressFamily(AddressFamily addressFamily);

    /**
     * @brief Recovers the terminal state from saved configurations.
     *
     * Retrieves a list of previously executed commands from persistent storage (e.g., XML files),
     * executes them to restore the terminal's state, and introduces delays for stability.
     */
    void recoverState();

    // Services
    void runEigrp();    ///< Runs the EIGRP service in a seperate thread
    void runOspf();     ///< Runs the OSPF service in a seperate thread
    void runBgp();      ///< Runs the BGP service in a seperate thread
    void runRip();      ///< Runs the RIP service in a seperate thread

    // Member variables
    float interfaceID;		///< Unique identifier for interfaces
    uint32_t routingProtocolID;	        ///< ID of the current routing protocol
	
    std::vector<std::string> globalCommandList{"?", "vk_tab"}; ///< List of global commands

    std::vector<std::string> commandHistory;    ///< History of previous entered commands

    DoTime timeManager;                         ///< Manages time-related functionality

    size_t paginationCount = 10;                ///< Pagination count for command help

    std::vector<std::string> currentPatterns;   ///< Current matching patterns
    std::string currentPattern;                 ///< Current matching pattern
    std::string endCommandString;	        ///< String for marking the end of a command
    std::string previousMatch;		        ///< Previous successfull command match
    std::string currentCommand;		        ///< Current command being processed
    std::string currentSubMode;		        ///< Current sub-mode (e.g., specific interface or protocol)

    nlohmann::json commandTree;		        ///< JSON structure holding the command hierarchy.
    nlohmann::json* currentDirectory;	        ///< Current directory in the command tree.
    nlohmann::json* workingDirectory;           ///< Working directory in the JSON structure.
    std::vector<json*> tempDir;                 ///< Temporary directory for creating temporary directories.
    std::vector<json*> loosePtrs;               ///< Temporary directory for removing loose pointers.
    
    std::vector<std::string> recursiveHistory;  ///< Recursive history for using a command once.

    std::vector<std::string> executionHistory;	///< History of executed commands

    bool isRunning = true;		        ///< Terminal run state
    bool error = false;                         ///< Indicates if the next command is invalid.
    bool endOfCommand = false;		        ///< Indicates if the command has reached its end
    bool isNextWordHelpRequested = false;       ///< Indicated if help is requested for the next word
    bool isMatchSuccessful = false;             ///< Indicates if a command match was successfull
    bool isLineBasedInput = false;	        ///< indicates if input is line-based
    bool isHelpModeActive = false;	        ///< Indicates if help mode is active
    bool isCommandValid = false;	        ///< Indicates if the command is valid
    bool isPatternMatching = false;	        ///< Indicates if the input matches a pattern
    bool isPatternMatchEnd = false;	        ///< Indicates the end of a matching pattern
    bool isModeChanged = false;		        ///< Indicates if the operational mode has changed
    bool isExitCommand = false;                 ///< Indicates if the command return to the previous mode
    bool isCommandInvalid = false;              ///< Indicates if a command is invalid
    bool isCommandExecutionSuccessful = false;  ///< Indicates if the command was successful
    bool isGlobalCommandExecution = false;      ///< Indicates if a global command is being executed
    bool isDebugModeEnabled = false;            ///< Indicates if in Debug mode
    bool attemptingGlobalCommand = false;       ///< Indicates if a global command is being attempted

    std::condition_variable stateCondition;     ///< Condition variabel for thread synchronization
    
    static std::string defaultMode;

    VirtualRouter* currentVrf;
};

#endif // TERMINAL_H
