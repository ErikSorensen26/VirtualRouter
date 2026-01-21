// CliSession.h

#ifndef CLI_SESSION_H
#define CLI_SESSION_H

#include <string>
#include <vector>
#include <json.hpp>
#include <Time.h>
#include <Mode.hpp>
#include "Console.h"

#include <ExecutionContext.hpp>

#define VARIABLE_OBJ "VARIABLES"

#define SUBCOMMAND_ARRAY "subcommands"
#define DESCRIPTION "description"
#define SUPPORT_STATUS "support"
#define COMMAND_NAME "name"
#define COMMAND_PROPERTIES "properties"

enum class AddressFamily : uint8_t;
class CommandProcessor;
class CliEngine;
class VirtualRouter;
class Interface;
class Configs;
struct Com;
namespace Eigrp
{
    class Eigrp;
    class EigrpNamed;
    class EigrpInterface;
}

namespace Protocol::Dhcp
{
struct DhcpNetworkConfig;  ///< Forward declaration of Protocol::Dhcp::DhcpNetworkConfig
}

/**
 * @class CliSession
 * @brief Represents an interactive command-line session managed by a CliEngine.
 *
 * CliSession implements all interactive CLI behavior, including:
 * - Mode handling (user, privileged, configuration, interface, routing, AF-specific modes)
 * - Input parsing, tokenization, help display, and tab-completion
 * - Command normalization and dispatch to CommandProcessor
 * - Pagination and contextual command discovery based on the engine's JSON command tree
 *
 * Architectural role:
 * - Acts as the user-facing frontend to the CLI system.
 * - Coordinates with CliEngine for command metadata, schemas, and global state.
 * - Delegates execution to CommandProcessor after normalizing commands.
 *
 * Concurrency:
 * - Not thread-safe. All interaction must occur from a single thread.
 *
 * Memory model:
 * - Owns CommandProcessor.
 * - Holds non-owning references/pointers into CliEngine-managed JSON trees.
 * - Cleans up temporary JSON nodes created during pattern expansion.
 *
 * Invariants:
 * - `workingDirectory` always points to the active JSON command node for the current mode.
 * - After normalizeCommand(), `commandHistory` holds the parsed form of the command.
 * - Mode transitions always update prompt and schema pointer consistency.
 */
class CliSession : public Console
{
public:

    /**
     * @brief Creates a session using an internally created Console.
     *
     * Initializes CLI mode state, selects default mode, resets parsing state,
     * initializes console backend, constructs CommandProcessor, and sets the
     * default VRF.
     *
     * @param engine Reference to the shared CliEngine.
     * @param enableDebug Enables verbose debugging behavior for development use.
     */
    CliSession(CliEngine& engine, bool enableDebug = false);

    /**
     * @brief Creates a session using a caller-supplied console.
     *
     * Behaves like the primary constructor but uses the provided IConsole backend.
     * Useful for testing or specialized I/O environments.
     *
     * @param engine Reference to the shared CliEngine.
     * @param term External IConsole instance (not owned).
     */
    CliSession(CliEngine& engine, IConsole* term);

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
     * Recomputes the prompt based on hostname and active CLI mode. Applies queued
     * auto-completion/help output when needed.
     */
    void handlePrompt();

    /**
     * @brief Processes a full line of input: normalization, execution, pagination.
     *
     * Handles control sequences, help triggers, pagination continuation, and
     * delegates execution to executeCommand().
     *
     * @param input Optional raw input supplied programmatically.
     * @return true if session remains active; false if execution stops or fails.
     */
    bool handleInput(std::string input = "");

    /**
     * @brief Switches the CLI session into a new mode and updates all related context.
     *
     * This method rebinds the session to a new command namespace, updates the prompt,
     * refreshes schema references, and manages mode-history tracking for nested
     * configuration contexts. It does not validate semantic correctness beyond
     * ensuring that the target mode exists in the command tree.
     *
     * ### Behavior
     * - Verifies the mode exists and is backed by a command array.
     * - Updates `modeConfig.currentMode`, prompt, and directory pointers.
     * - Loads the mode-specific schema into either the active or temporary schema slot.
     * - Maintains `modeHistory` to support returning from nested configuration modes.
     *
     * @param newMode Name of the target CLI mode.
     * @param processing Whether this change occurs during command processing
     *                   (affects whether `modeSchema` or `tempModeSchema` is used).
     * @return true if the mode exists and was applied; otherwise false.
     */
    bool changeMode(std::string& newMode, bool processing = false); ///< Mode transition logic.

    /**
     * @brief Marks the session as exiting the current mode and performs a mode change.
     *
     * This helper sets the internal exit flag and delegates the actual transition
     * to `changeMode()`. It is used by commands that unwind one level of the
     * mode hierarchy (e.g., leaving interface or router configuration contexts).
     *
     * @param newMode The mode to transition into after exiting the current one.
     */
    inline void exitMode(std::string& newMode) { isExitCommand = true; changeMode(newMode); }

    /**
     * @brief Resets all transient command-processing state for a new input cycle.
     *
     * This method clears all match buffers, resets command-validation flags,
     * restores directory pointers, and prepares the session for parsing and
     * executing the next user command. It is invoked before each command
     * evaluation to guarantee a consistent processing baseline.
     *
     * ### Responsibilities
     * * - Clears previous match and pattern trackers.
     * * - Resets mode/command status flags (help mode, validity markers, global-command attempts).
     * * - Resets transient I/O and parsing state (line-based mode, end-of-command).
     * * - Restores `currentDirectory` to the active working mode.
     * * - Resets negation state inside the command processor.
     *
     * ### Postconditions
     * * - All processing flags are in a neutral state.
     * * - No leftover state from prior commands can influence the next one.
     */
    void initializeProcessingState(); ///< Resets per-command parsing flags.

    /**
     * @brief Executes a normalized CLI command within the current mode.
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

    Cli::ExecutionManager execution;

    std::vector<std::string> commandHistory;    ///< Last normalized command tokens.

    std::vector<std::string> currentPatterns;   ///< Active pattern stack.
    std::string currentPattern;                 ///< Current pattern name.
    std::string endCommandString;	        ///< End-of-command marker.
    std::string previousMatch;		        ///< Last matched command/pattern.
    std::string currentCommand;		        ///< Current raw command.
    std::string currentSubMode;		        ///< Interface/routing mode subtype.

    const nlohmann::ordered_json* currentDirectory; ///< Active command directory.
    const nlohmann::ordered_json* workingDirectory = nullptr; ///< Working JSON node.

    std::vector<const nlohmann::ordered_json*> tempDir; ///< Temp expansion buffer.
    std::vector<nlohmann::ordered_json*> loosePtrs;     ///< Heap-owned temp nodes.

    std::vector<std::string> recursiveHistory;  ///< Recursion control history.
    std::vector<std::string> executionHistory;  ///< Past executed commands.

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

    std::vector<Com> paginationList; ///< Commands queued for pagination.
    size_t maxNameLength = 0;        ///< Longest visible command name.

    std::string currentPrompt;          ///< Indicates the current prompt.

    // CONFIG OBJECTS

    nlohmann::ordered_json *prevConfig;     ///< Pointer to the previous configuration node
    nlohmann::ordered_json* configNode = nullptr;       ///< Pointer to the current configuration node.
    std::vector<nlohmann::ordered_json*> modeHistory;   ///< History of configuration nodes for mode management
    nlohmann::ordered_json* modeSchema = nullptr;       ///< Pointer to the current mode's schema
    nlohmann::ordered_json* tempModeSchema = nullptr;   ///< Temporary pointer for schema operations.

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

#endif // CLI_SESSION_H

