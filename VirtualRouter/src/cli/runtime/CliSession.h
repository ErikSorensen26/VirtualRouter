// CliSession.h

#ifndef CLI_SESSION_H
#define CLI_SESSION_H

#include <string>
#include <vector>
#include <json.hpp>

#include "Console.h"
#include "cli/modes/Mode.hpp"
#include "cli/execution/ExecutionContext.hpp"

namespace core { class VirtualRouter; }
namespace interface { class Interface; }

#define VARIABLE_OBJ "VARIABLES"

#define CLI_JSON_SUBCOMMAND_ARRAY "subcommands"
#define CLI_JSON_DESCRIPTION "description"
#define CLI_JSON_SUPPORT_STATUS "support"
#define CLI_JSON_COMMAND_NAME "name"
#define CLI_JSON_COMMAND_PROPERTIES "properties"

namespace types { enum class AddressFamily : uint8_t; }
namespace routing { namespace eigrp { class Eigrp; class EigrpNamed; class EigrpInterface; } }
namespace services { namespace protocol { namespace dhcp { struct DhcpNetworkConfig; } } }

namespace cli
{
class CommandProcessor;
class CliEngine;
class Configs;
struct Com;

/**
 * @class Terminal
 * @brief Represents a command-line terminal interface.
 *
 * The terminal class provides functionalities to interact with the user via the command line.
 * It supports various commands, modes, and maintains the state between sessions.
 */
class CliSession : public Console
{
public:
    // Friend classes for testing
    friend class Internal_CliTest;
    friend class CommandProcessor;
    friend class CliEngine;
    friend Configs;

    /**
     * @brief Constructor for the Terminal class.
     * 
     * Initializes the terminal by setting up debugging options, loading command configurations,
     * setting the default mode, and restoring the previous state if available.
     * 
     * @param engine The CLI engine managing all sessions.
     * @param enableDebug A boolean flag to enable or disable debug mode.
     */
    CliSession(CliEngine& engine, ConsoleController& controller, bool enableDebug = false);

    /**
     * @brief Captures and processes user input in the terminal.
     * 
     * This function reads the user's input, processes IPv6 addresses if applicable,
     * handles shortcuts (e.g., Ctrl-Z for mode switching), and executes valid commands.
     *
     * @param input String for giving manual input
     */
    void handlePrompt();
    bool handleInput(std::string input = "");

    /**
     * @brief Changes the terminal's operational mode.
     *
     * Updates the current mode of the terminal, adjusts the working directory based on the new mode,
     * and manages mode history for potential state restoration.
     *
     * @param newMode A reference to the string representing the new mode to switch to.
     * @param processing A boolean flag indicating whether the mode change is part of command processing. Defaults to false.
     */
    template <CliMode T, typename... Args>
    bool changeMode(Args&&... args);

    /**
     * @brief Exits the current mode and switches to a new mode.
     *
     * Marks the exit command and changes the mode accordingly.
     *
     * @param newMode The mode to switch to upon exiting the current mode.
     */
    template <CliMode T, typename... Args>
    inline void exitMode(Args&&... args) { isExitCommand = true; changeMode<T>(std::forward<Args>(args)...);}

    // Member variables
    float interfaceID;		///< Unique identifier for interfaces

    bool isList = false;
    bool textLine = false;
    bool isDebugModeEnabled = false;
    bool isModeChanged = false;		        ///< Indicates if the operational mode has changed.
	
private:

    /**
     * @brief Executes a single command
     *
     * Processes the given command string and performs the corresponding action within the cli
     *
     * @param command The command string to process and execute.
     * @return boolean Indicates whether the execution was a success or not.
     */
    bool executeCommand(std::string& command);
    std::vector<std::string> compileCommandStream(const std::string& command);
    bool executeModeParser(const std::vector<std::string>& tokens);
    bool processConfigPersistence(const std::vector<std::string>& tokens, CliMode preMode);

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
     * @brief Determines if the current command is a "no" command
     *
     * Checks whether the parsed words represent a "no" command that should be executed.
     *
     * @param parsedWords A vector of strings representing the parsed command words.
     * @return true If the command is a valid "no" command; otherwise, false.
     */
    bool isNoCommand(const std::vector<std::string>& parsedWords);

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
     * @param previousCommands A vector of the previous command(s) used.
     * @return std::vector<com> A vector of available commands matching the user input.
     */
    std::vector<Com> getAvailableCommands(const std::string& userInput, bool inPriviledgedMode, std::vector<Com>& previousCommands);

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
    bool isValidCommandDirectory(const nlohmann::ordered_json* directory);

    /**
     * @brief Handles pagination for displaying large lists of commands.
     *
     * If the number of lines exceeds a certain threshold (e.g., 10 lines), prompts the user to continue
     * or quit viewing additional commands.
     *
     * @param ch A character input from the user to control pagination.
     * @return true If pagination continues; otherwise, false to stop displaying more lines.
     */
    bool handlePagination(char ch = '\0');

    /**
     * @brief Updates the global configuration history from the local history.
     *
     * Clears the previous global configuration and appends the current root node.
     */
    void historyToGlobal();

    bool setCommandDirectory(std::span<const std::string_view>& dir);
    CliMode getMode();

    cli::ExecutionManager execution;

    std::vector<std::string> commandHistory;    ///< History of previous entered commands

    std::vector<std::string> currentPatterns;   ///< Current matching patterns
    std::string currentPattern;                 ///< Current matching pattern
    std::string endCommandString;	        ///< String for marking the end of a command
    std::string previousMatch;		        ///< Previous successfull command match
    std::string currentCommand;		        ///< Current command being processed
    std::string currentSubMode;		        ///< Current sub-mode (e.g., specific interface or protocol)

    const nlohmann::ordered_json* currentDirectory;	        ///< Current directory in the command tree.
    const nlohmann::ordered_json* workingDirectory = nullptr;   ///< Working directory in the JSON structure.

    std::vector<const nlohmann::ordered_json*> tempDir;                 ///< Temporary directory for creating temporary directories.
    std::vector<nlohmann::ordered_json*> loosePtrs;               ///< Temporary directory for removing loose pointers.

    std::vector<std::string> recursiveHistory;  ///< Recursive history for using a command once.

    std::vector<std::string> executionHistory;	///< History of executed commands

    bool isRunning = true;		        ///< Terminal run state
    bool error = false;                         ///< Indicates if the next command is invalid.
    bool endOfCommand = false;		        ///< Indicates if the command has reached its end.
    bool isNextWordHelpRequested = false;       ///< Indicated if help is requested for the next word.
    bool isMatchSuccessful = false;             ///< Indicates if a command match was successfull.
    bool isLineBasedInput = false;	        ///< indicates if input is line-based.
    bool isHelpModeActive = false;	        ///< Indicates if help mode is active.
    bool isCommandValid = false;	        ///< Indicates if the command is valid.
    bool isPatternMatching = false;	        ///< Indicates if the input matches a pattern.
    bool isPatternMatchEnd = false;	        ///< Indicates the end of a matching pattern.
    bool isExitCommand = false;                 ///< Indicates if the command return to the previous mode.
    bool isCommandInvalid = false;              ///< Indicates if a command is invalid.
    bool isCommandExecutionSuccessful = false;  ///< Indicates if the command was successful.
    bool isGlobalCommandExecution = false;      ///< Indicates if a global command is being executed.
    bool attemptingGlobalCommand = false;       ///< Indicates if a global command is being attempted.

    std::vector<Com> paginationList;            ///< List of commands for pagination.
    size_t maxNameLength = 0;                   ///< Max command size for pagination.

    std::string currentPrompt;          ///< Indicates the current prompt.

    // CONFIG OBJECTS

    const nlohmann::ordered_json *prevConfig;     ///< Pointer to the previous configuration node
    const nlohmann::ordered_json* configNode = nullptr;       ///< Pointer to the current configuration node.
    std::vector<const nlohmann::ordered_json*> modeHistory;   ///< History of configuration nodes for mode management
    const nlohmann::ordered_json* modeSchema = nullptr;       ///< Pointer to the current mode's schema
    const nlohmann::ordered_json* tempModeSchema = nullptr;   ///< Temporary pointer for schema operations.

public:
    CliEngine& engine;
};

template <CliMode T, typename... Args>
bool CliSession::changeMode(Args&&... args)
{
    std::span<const std::string_view> path = getPath(T);
    if (!setCommandDirectory(path)) return false;
    execution.changeMode<T>(std::forward<Args>(args)...);
    return true;
}
}

#endif // CLI_SESSION_H
