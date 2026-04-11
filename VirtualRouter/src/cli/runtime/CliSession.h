/**
 * @file CliSession.h
 * @brief Per-user CLI session: command parsing, mode navigation, and execution dispatch.
 */

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

#define CLI_JSON_SUBCOMMAND_ARRAY   "subcommands"
#define CLI_JSON_DESCRIPTION        "description"
#define CLI_JSON_SUPPORT_STATUS     "support"
#define CLI_JSON_COMMAND_NAME       "name"
#define CLI_JSON_COMMAND_PROPERTIES "properties"

namespace types { enum class AddressFamily : uint8_t; }
namespace routing { namespace eigrp { class Eigrp; class EigrpNamed; class EigrpInterface; } }
namespace services { namespace protocol { namespace dhcp { struct DhcpNetworkConfig; } } }

namespace cli
{
class CommandProcessor;
class CliEngine;
class Configs;
struct Token;
struct Com;

/**
 * @brief Represents a single interactive CLI user session.
 *
 * One `CliSession` exists per connected user. It owns the full pipeline from
 * raw terminal input to mode-specific command execution:
 * - Tokenizing and resolving user input against the JSON command tree
 * - Driving tab-completion and inline help (`?`)
 * - Tracking the active CLI mode and navigating the mode stack
 * - Dispatching resolved token lists to the active mode parser
 * - Paginating command output through the terminal
 *
 * ## Architectural Role
 * `CliSession` is the boundary between the terminal layer (@ref Console /
 * @ref ConsoleController) and the mode-specific command parsers. It holds no
 * protocol or routing state directly — it delegates execution to
 * @ref ExecutionManager, which routes tokens to the correct mode handler.
 *
 * The shared command tree and global engine state live in @ref CliEngine;
 * `CliSession` only holds per-user view state (current mode, working directory
 * pointer, prompt string, pagination list).
 *
 * ## Lifecycle & Ownership
 * Created by @ref CliEngine, one per user connection. Construction
 * immediately enters `UserExec` mode and initializes the terminal. There is
 * no explicit teardown sequence — destruction of the session is sufficient.
 */
class CliSession : public Console
{
public:
    friend class Internal_CliTest;
    friend class CommandProcessor;
    friend class CliEngine;
    friend Configs;

    /**
     * @brief Constructs a session and enters the initial UserExec mode.
     *
     * Registers the session with the engine's command tree, sets up the
     * console, and prints the initialization banner. The session is ready
     * to accept input immediately after construction.
     *
     * @param engine      The shared CLI engine (command tree, global state).
     * @param controller  Terminal controller for I/O.
     * @param enableDebug Enables debug-mode output; only meaningful in DEBUG builds.
     */
    CliSession(CliEngine& engine, ConsoleController& controller, bool enableDebug = false);

    /**
     * @brief Prints the prompt and pre-fills any pending nextLine input.
     *
     * Called after each completed command (or help/tab response) to redisplay
     * the prompt. If a `nextLine` string is queued (e.g. from tab-completion),
     * it is injected into the input buffer so the user sees the partial command.
     */
    void handlePrompt();

    /**
     * @brief Reads one line of input and executes the resulting command.
     * @param input  Optional injected input (for testing); empty = real terminal.
     * @return true if the command executed successfully.
     */
    bool handleInput(std::string input = "");

    /**
     * @brief Transitions to a new CLI mode and pushes the current mode onto the nav stack.
     *
     * Updates the working directory pointer and prompt string, captures the current
     * mode into the nav stack, then delegates to @ref ExecutionManager::changeMode.
     * Use this for all normal sub-mode entries (interface, router, address-family, etc.)
     * so that `popMode()` can return here automatically.
     *
     * @tparam T  Target @ref CliMode enum value.
     * @tparam S  Registry type for the new mode.
     */
    template <CliMode T, typename S>
    requires config::IsSubRegistry<S>
    bool changeMode(S& configs)
    {
        if (execution.hasMode() && navTop < NAV_STACK_DEPTH)
            navStack[navTop++] = { execution.captureCurrentMode(), currentPrompt, workingDirectory, configNode };

        std::span<const std::string_view> path = getPath(T);
        if (!setCommandDirectory(path))
        {
            if (navTop > 0) --navTop;
            return false;
        }
        execution.changeMode<T, S>(configs);
        return true;
    }

    /**
     * @brief Pops the navigation stack and restores the previous CLI mode.
     *
     * Called by `exit` handlers. Restores the mode, prompt, working directory, and
     * config node exactly as they were when `changeMode` entered the current mode.
     *
     * @return True if there was a mode to pop; false if already at the bottom.
     */
    bool popMode();

    /**
     * @brief Transitions to a mode without pushing or popping the nav stack.
     *
     * Used for hard resets (`end`, Ctrl-Z) that jump to a fixed mode regardless
     * of navigation depth. Clears the entire nav stack first so subsequent
     * `popMode()` calls find an empty stack.
     *
     * @tparam T  Target @ref CliMode enum value.
     * @tparam S  Registry type for the new mode.
     */
    template <CliMode T, typename S>
    requires config::IsSubRegistry<S>
    bool resetAndChangeMode(S& configs)
    {
        navTop = 0;
        std::span<const std::string_view> path = getPath(T);
        if (!setCommandDirectory(path)) return false;
        execution.changeMode<T, S>(configs);
        return true;
    }

    // PUBLIC STATE (read by command handlers after execution)

    bool  textLine           = false; ///< Set when the last command contained a LINE-pattern token.
    bool  isModeChanged      = false; ///< Set when the last command caused a mode transition.

#ifdef DEBUG
    bool  isDebugModeEnabled = false;
#endif

    CliEngine& engine;

private:
    /// Outcome of one @ref parseInput call, carrying everything @ref executeCommand needs.
    struct ParseResult
    {
        enum class Status
        {
            EMPTY,      ///< Nothing to process (blank input).
            OK_,        ///< Fully resolved — execute resolvedCommand.
            HELP,       ///< Display helpList and re-prompt with nextLine.
            TAB,        ///< Autocomplete: re-prompt with nextLine.
            INVALID,    ///< Bad token — print caret at markerCommand position.
            AMBIGUOUS,  ///< Ambiguous token — print message.
            INCOMPLETE, ///< Valid prefix but command not finished.
            GLOBLA_CMD, ///< Standalone ? or vk_tab — already handled.
            DO_COMMAND, ///< "do <rest>" — re-execute in privileged mode.
        };

        Status status = Status::EMPTY;

        // Ok path
        std::vector<Token> tokens;
        bool negate    = false;
        bool defaulted = false;

        // Help / Tab path
        std::vector<Com> helpList;
        std::string      nextLine;

        // Invalid path
        std::string markerCommand; ///< Accumulated command text before the ^ marker.

        // Ambiguous path
        std::string ambiguousToken;

        // GlobalCmd path
        std::string globalToken;

        // DoCommand path
        std::string doRemainder;
    };

    // PARSING

    /// Parse rawInput against the current command tree.
    ParseResult parseInput(std::string& rawInput);

    /// Normalise, parse, and execute one command string.
    bool executeCommand(std::string& command);

    /// Dispatch a token vector to the active mode parser.
    bool executeModeParser(const std::span<Token> tokens);

    // SPECIAL COMMAND FLOWS

    /// Execute remainder in privileged mode, then restore previous mode.
    bool tryDoCommand(const std::string& remainder);

    /// Re-attempt a failed command in GlobalConfiguration, then restore.
    bool tryGlobalCommand(const std::string& rawInput);

    // MODE MANAGEMENT

    bool    setCommandDirectory(std::span<const std::string_view>& dir);
    CliMode getMode();
    void    historyToGlobal();

    // DISPLAY

    void displayCommands(std::vector<Com>& list); ///< Enqueues list for pagination.
    bool handlePagination(char ch = '\0');

    // SESSION-LEVEL STATE

    /// @brief One entry in the navigation history stack.
    struct NavFrame
    {
        cli::NavEntry             executorEntry;   ///< Execution state (mode, dispatch, construct, config ptr).
        std::string               savedPrompt;     ///< Prompt string active when this mode was entered.
        const nlohmann::ordered_json* savedWorkingDir = nullptr; ///< Command-tree node for this mode.
        const nlohmann::ordered_json* savedConfigNode = nullptr; ///< Config-tree root for this mode.
    };

    static constexpr size_t NAV_STACK_DEPTH = 10; ///< Maximum navigation depth (UserExec → deepest sub-mode).

    NavFrame navStack[NAV_STACK_DEPTH]; ///< Fixed-size navigation history; avoids heap allocation.
    size_t   navTop = 0;               ///< Number of valid frames currently on the stack.

    cli::ExecutionManager execution; ///< Owns the active mode object and dispatches token lists.

    const nlohmann::ordered_json* workingDirectory = nullptr; ///< Current command-tree array for the active mode.

    // CONFIG-TREE TRACKING

    const nlohmann::ordered_json* prevConfig = nullptr; ///< Command-tree root before the last tryGlobalCommand detour.
    const nlohmann::ordered_json* configNode = nullptr; ///< Root node of the engine's full command tree.
    std::vector<const nlohmann::ordered_json*> modeHistory; ///< Stack of config-tree roots visited during mode transitions.

    std::string currentPrompt; ///< Mode-specific prompt suffix appended to the hostname.

    // PAGINATION

    std::vector<Com> paginationList; ///< Remaining commands to display; non-empty while paging.
    size_t           maxNameLength = 0; ///< Widest name in paginationList, used to align descriptions.
    void*            previousMode = nullptr; ///< Pointer to the previous config object.

    std::vector<std::string> executionHistory; ///< Resolved command strings for commands that mutate list-type config.
};
} // namespace cli

#endif // CLI_SESSION_H
