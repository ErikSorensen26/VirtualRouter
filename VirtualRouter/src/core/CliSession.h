// CliSession.h

#ifndef CLI_SESSION_H
#define CLI_SESSION_H

#include <string>
#include <vector>
#include <json.hpp>
#include <Time.h>
#include <Mode.hpp>
#include "Console.h"

#define VARIABLE_OBJ "VARIABLES"

#define SUBCOMMAND_ARRAY "subcommands"
#define DESCRIPTION "description"
#define SUPPORT_STATUS "support"
#define COMMAND_NAME "name"
#define COMMAND_PROPERTIES "properties"

enum class AddressFamily : uint8_t; ///< Forward declaration of AddressFamily
class CommandProcessor; ///< Forward declaration of CommandProcessor
class CliEngine;        ///< Forward declaration of CliEngine
class VirtualRouter;    ///< Forward declaration of VirtualRouter
class Interface;        ///< Forward declaration of Interface
class Configs;          ///< Forward declaration of Configs
struct Com;             ///< Forward declaration of Com
struct ModeConfig;      ///< Forward declaration of ModeConfig

namespace Eigrp
{
class Eigrp;                ///< Forward declaration of Eigrp::Eigrp
struct EigrpNamed;          ///< Forward declaration of Eigrp::EigrpNamed
class EigrpInterface;       ///< Forward declaration of Eigrp::EigrpInterface
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
     * @brief Releases session-owned resources, including CommandProcessor.
     */
    ~CliSession();

    /**
     * @brief Prints the session prompt and resets input state.
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
     * Performs preprocessing, word segmentation, LINE-mode handling, and dispatches
     * the resulting command stream to the appropriate mode handler. Handles config
     * persistence (save/delete), exit-mode transitions, and command validation flags.
     *
     * @param command Raw command string to execute; normalized form is written back.
     * @return true on successful execution or valid mode transition; false otherwise.
     */
    bool executeCommand(std::string& command);

public:

    CommandProcessor* commandProcessor = nullptr; ///< Session-owned command executor.

    CliEngine& engine; ///< Non-owning reference to the global engine.

    ModeConfig modeConfig; ///< Tracks mode, config-node, and mode history.

    float interfaceID;		///< Interface identifier (session context).
    uint32_t routingProtocolID; ///< Active routing protocol ID.

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

    bool isRunning = true;		        ///< Overall session run state; false stops further processing.
    bool error = false;                         ///< Indicates that the next command is invalid or in error context.
    bool endOfCommand = false;		        ///< Indicates that the parser has reached an end-of-command token.
    bool isNextWordHelpRequested = false;       ///< Indicated that help is requested for the next token.
    bool isMatchSuccessful = false;             ///< Indicates whether a unique command match has been achieved.
    bool isLineBasedInput = false;	        ///< indicates whether remaining input is treated as a free-form line.
    bool isHelpModeActive = false;	        ///< Indicates whether the current command is in help mode.
    bool isCommandValid = false;	        ///< Indicates whether the current command sequence is valid
    bool isPatternMatching = false;	        ///< Indicates that the parser is in pattern-matching mode.
    bool isPatternMatchEnd = false;	        ///< Indicates that the current pattern marks the end of the match.
    bool isModeChanged = false;		        ///< Indicates that the operational mode was changed during processing.
    bool isExitCommand = false;                 ///< Indicates that the current command triggers a mode exit.
    bool isCommandInvalid = false;              ///< Indicates that the current command is known to be invalid.
    bool isCommandExecutionSuccessful = false;  ///< Indicates that command execution completed successfully.
    bool isGlobalCommandExecution = false;      ///< Indicates the current command is being executed as a global command.
    bool isDebugModeEnabled = false;            ///< Indicates that this sessions running with debug enabled.
    bool attemptingGlobalCommand = false;       ///< Indicates that a global command fallback has been attempted.

    std::vector<Com> paginationList; ///< Commands queued for pagination.
    size_t maxNameLength = 0;        ///< Longest visible command name.

    std::string currentPrompt; ///< Suffix for prompt (mode string).
    std::string prevMode;      ///< Previously active mode.

    bool isList = false;
    bool textLine = false;

    nlohmann::ordered_json *prevConfig = nullptr; ///< Used during global fallback.

private:

    std::string normalizeCommand(const std::string& command); ///< Converts raw input → canonical command.

    void appendLineBasedCommand(const std::vector<std::string>&, size_t,
                                std::string&, std::string&); ///< Handles line-based tokens.

    void processNonLineBasedWord(std::string&, std::vector<Com>&,
                                 std::string&, std::string&, std::string&,
                                 const std::string&, bool&); ///< Processes structured tokens.

    bool detectHelpTriggers(const std::vector<std::string>&); ///< Detects "?" or tab.
    bool isNoCommand(const std::vector<std::string>&); ///< Checks for negation command.
    bool isDoCommand(const std::vector<std::string>&); ///< Checks for "do" command.
    std::string executeDoCommand(std::string); ///< Executes command in exec mode.

    bool handleHelpQuestion(const std::string&, std::vector<Com>&,
                            const std::string&, std::string&, std::string&, std::string&); ///< Handles "?".
    bool handleTabCompletion(const std::string&, std::vector<Com>&,
                             const std::string&, std::string&, std::string&, std::string&); ///< Handles tab.

    bool attemptGlobalCommand(const std::string&); ///< Attempts global config fallback.

    void handleInvalidInputMarker(const std::string&); ///< Prints invalid input marker.
    void handleAmbiguousInputMarker(const std::string&); ///< Prints ambiguous command marker.

    void matchCommand(const std::string&, const std::string&, const std::vector<Com>&,
                      std::vector<Com>&, std::string&, std::string&, std::string&, bool&); ///< Match logic.

    std::vector<Com> getAvailableCommands(const std::string&, bool, std::vector<Com>&); ///< JSON-based lookup.

    bool isGlobalCommand(std::string&); ///< Checks if name is global command.
    void displayAvailableCommands(std::vector<Com>); ///< Prepares pagination list.

    std::string getLastWord(const std::string&); ///< Utility tokenizer.
    std::vector<std::string> splitIntoWords(const std::string&); ///< Input tokenizer.
    std::string trimString(std::string); ///< Removes leading spaces.

    bool matchInputPattern(const std::string&, const std::string&); ///< Pattern matcher.
    bool isNumeric(const std::string&); ///< Checks integer type.
    bool isValidCommandDirectory(const nlohmann::ordered_json*); ///< JSON directory test.

    bool handlePagination(char = '\0'); ///< Pagination engine.

    void configureInterfaceMode(std::string&); ///< Interface mode setup.
    void configureRoutingMode(std::string, bool classicV6 = false); ///< Routing mode setup.
    void configureAddressFamily(AddressFamily); ///< Address-family mode narrowing.

    void historyToGlobal(); ///< Resets mode history → root.
};

#endif // CLI_SESSION_H

