/**
 * @file CliSession.h
 * @brief Per-user CLI session: command parsing, mode navigation, and execution dispatch.
 */

#ifndef CLI_SESSION_H
#define CLI_SESSION_H

#include <string>
#include <vector>
#include <cstddef>

#include "cli/terminal/Console.h"
#include "cli/modes/Mode.hpp"
#include "cli/execution/ExecutionContext.hpp"
#include "cli/tree/Command.h"
#include "cli/tree/ModeEntry.h"
#include "TreeNavigator.hpp"

class Internal_CliTest;

namespace core { class VirtualRouter; }
namespace interface { class Interface; }

namespace types { enum class AddressFamily : uint8_t; }
namespace routing { namespace eigrp { class Eigrp; class EigrpNamed; class EigrpInterface; } }
namespace services { namespace protocol { namespace dhcp { struct DhcpNetworkConfig; } } }

namespace cli
{
class CommandProcessor;
class CliEngine;
class Configs;
struct Token;


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
    friend class ::Internal_CliTest;
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

    CliSession(const CliSession&) = delete;
    CliSession& operator=(const CliSession&) = delete;
    CliSession(CliSession&&) = delete;
    CliSession& operator=(CliSession&&) = delete;

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


    // MODE MANAGEMENT (forwards to the navigation stack; called by command handlers)

    /// Enters a sub-mode, pushing the current one so `popMode` can return here.
    template <CliMode T, typename S>
    requires config::IsSubRegistryWrapper<S>
    bool changeMode(S& configs) { return nav.changeMode<T, S>(configs); }

    /// Jumps to a fixed mode and clears the nav stack (`end`, Ctrl-Z).
    template <CliMode T, typename S>
    requires config::IsSubRegistryWrapper<S>
    bool resetAndChangeMode(S& configs) { return nav.resetAndChangeMode<T, S>(configs); }

    /// Returns to the previous mode; false if already at the bottom.
    bool popMode() { return nav.popMode(); }

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
            GLOBAL_CMD, ///< Standalone ? or vk_tab — already handled.
            DO_COMMAND, ///< "do <rest>" — re-execute in privileged mode.
        };

        Status status = Status::EMPTY;

        // Ok path
        std::vector<Token> tokens;
        bool negate    = false;
        bool defaulted = false;

        // Help / Tab path
        std::vector<tree::Command> helpList;
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

    CliMode getMode();

    // DISPLAY

    void displayCommands(std::vector<tree::Command>& list);
    bool handlePagination(char ch = '\0');

    cli::execution::ExecutionManager execution; ///< Owns the active mode object and dispatches token lists.

    // CONFIG-TREE TRACKING

    tree::Command prevConfig; ///< Command-tree root before the last tryGlobalCommand detour.
    tree::CommandTree& commandTree; ///< Root node of the engine's full command tree.
    TreeNavigation nav; 

    // PAGINATION

    std::vector<tree::Command> paginationList; ///< Remaining commands to display; non-empty while paging.
    size_t  maxNameLength = 0; ///< Widest name in paginationList, used to align descriptions.
    void*   previousMode = nullptr; ///< Pointer to the previous config object.

    std::vector<std::string> executionHistory; ///< Resolved command strings for commands that mutate list-type config.

    
};
} // namespace cli

#endif // CLI_SESSION_H
