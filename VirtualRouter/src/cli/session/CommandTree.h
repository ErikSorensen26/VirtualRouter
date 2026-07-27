/**
 * @file CommandTree.h
 * @brief JSON command-tree ownership, traversal, and token matching.
 *
 * Owns the command hierarchy loaded from `Commands.json` / `Commands.bin` and
 * everything that walks it: node views, per-token match state, and the
 * completion/help candidate lookup used by @ref CliSession while parsing input.
 */

#ifndef COMMAND_TREE_H
#define COMMAND_TREE_H

#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "cli/grammar/Token.hpp"

#define COMMAND_TREE_BIN "./configs/Commands.bin"
#define COMMAND_TREE "./configs/Commands.json"
#define CONFIG_SCHEMA "./configs/ConfigSchema.json"

#define VARIABLE_OBJ "VARIABLES"

#define CLI_JSON_SUBCOMMAND_ARRAY   "subcommands"
#define CLI_JSON_DESCRIPTION        "description"
#define CLI_JSON_SUPPORT_STATUS     "support"
#define CLI_JSON_COMMAND_NAME       "name"
#define CLI_JSON_COMMAND_PROPERTIES "properties"

namespace hardware { class HardwareManager; }

namespace cli
{
class FileSystem;

/**
 * @brief Metadata describing a CLI command, including semantic properties and support levels.
 * @ingroup CLI_RUNTIME
 *
 * Represents an element in a router’s command taxonomy. Command definitions may be generated
 * from schema files or static tables and serve as a basis for help systems, auto-completion,
 * and capability negotiation across operating modes.
 *
 * ### Architectural Role
 * - Acts as a normalized descriptor for CLI command components.
 * - Does not participate in execution; consumed by higher-level CLI engines.
 * - Serves as an input to schema-driven ordering within configuration trees.
 */
struct Com
{
    /**
     * @brief Enumerates match states when comparing commands.
     *
     * Used by @ref CliSession to indicate how the command matches to user input.
     */
    enum class Match
    {
        NONE,
        FULL,
        PARTIAL
    };

    std::string_view name{};          ///< Human-readable command keyword.
    std::string_view description{};   ///< Description of semantic behavior.
    std::vector<std::string_view> properties{}; ///< Arbitrary property labels (e.g., feature flags).
    Match match = Match::NONE;        ///< Indicates if the match was a partial.

    bool isExact() { return match == Match::FULL; }
    bool isPartial() { return match == Match::FULL || match == Match::PARTIAL; }
    bool isNone() { return !isPartial(); }
};

/// @brief Returns true when `p` is a user-supplied value placeholder rather than a keyword.
inline bool isVolatile(std::string_view p)
{
    return matchVolatilePattern(p) != P_NONE;
}

/**
 * @brief A command-tree node paired with an optional substituted subcommand array.
 *
 * `overrideSubCommands` lets a node expose a different child list than its own
 * `subcommands` key. This backs two cases: `recursive` nodes, which re-expose
 * their parent's list, and `<variable>` nodes, whose expansions each borrow the
 * placeholder node's subcommand array.
 */
struct NodeView
{
    const nlohmann::ordered_json* node;
    const nlohmann::ordered_json* overrideSubCommands = nullptr;

    const nlohmann::ordered_json* subCommands() const
    {
        if (overrideSubCommands)
            return overrideSubCommands;
        if (node && node->contains(CLI_JSON_SUBCOMMAND_ARRAY))
            return &(*node)[CLI_JSON_SUBCOMMAND_ARRAY];
        return nullptr;
    }
};

/// @brief How the current token compared against the candidates at this tree level.
enum class MatchState
{
    NONE,
    PARTIAL,
    EXACT,
    PATTERN
};

/// @brief Whether the command line so far forms a runnable command.
enum class CommandState
{
    IN_PROGRESS,
    COMPLETE,
    INVALID,
    AMBIGUOUS
};

/// @brief What the user asked for with this line: execution, help, or completion.
enum class InputMode
{
    NORMAL,
    HELP,
    TAB,
    LINE
};

/**
 * @brief Owns the JSON command tree and the variable expansions bound to it.
 *
 * Loaded once during engine initialization and immutable thereafter, so
 * concurrent reads from multiple sessions are safe.
 */
class CommandTree
{
public:
    /**
     * @brief Loads the command tree, preferring the CBOR cache over the JSON source.
     *
     * Reads `COMMAND_TREE_BIN` when present; otherwise parses `COMMAND_TREE` and
     * writes the CBOR cache back out for the next boot. On failure the tree is
     * left as an empty object.
     *
     * @param fs Filesystem used for both reads and the CBOR cache write.
     */
    void load(FileSystem& fs);

    /**
     * @brief Rewrites interface-index placeholders to match actual hardware.
     *
     * Replaces each interface type's variable entry with a `<0-N>` range sized
     * from the physical interface inventory, erasing types with no interfaces.
     * Mutates the tree in place during early initialization only.
     *
     * @param hwManager Hardware inventory supplying physical interface counts.
     */
    void initTree(hardware::HardwareManager& hwManager);

    /// @brief Returns the loaded tree. Immutable after initialization.
    const nlohmann::ordered_json& root() const { return tree; }

    /// @brief Returns true when `directory` is an object carrying a subcommand array.
    static bool isValidCommandDirectory(const nlohmann::ordered_json* directory);

private:
    nlohmann::ordered_json tree; ///< Full CLI grammar (CBOR or JSON sourced).
};

/**
 * @brief Per-line traversal state for one @ref CliSession::parseInput call.
 *
 * Holds the cursor into the command tree plus the match/command/input state
 * that advances as each token is consumed. Constructed fresh per input line;
 * `tempDir` owns the NodeView storage that expanded views point into, so it
 * must outlive any view vector built from it.
 */
struct ParseContext
{
    const CommandTree& commandTree;
    const Com& carriageReturnCommand;
    const nlohmann::ordered_json* workingDirectory;

    bool negateMode = false;
    bool defaultMode = false;

    MatchState matchState = MatchState::NONE;
    CommandState commandState = CommandState::IN_PROGRESS;
    InputMode inputMode = InputMode::NORMAL;

    const nlohmann::ordered_json* currentDirectory = nullptr;
    NodeView* currentNodeView = nullptr;

    std::string_view previousMatch;
    std::string_view currentPattern;
    std::string_view endCmdStr;

    bool eoc = false; // End of Command
    bool err = false; // Error
    bool nwh = false; // Next Word Help

    std::vector<NodeView> tempDir;

    ParseContext(const CommandTree& tree, const Com& cr, const nlohmann::ordered_json* wd)
        : commandTree(tree), carriageReturnCommand(cr), workingDirectory(wd)
    {
        currentDirectory = workingDirectory;
    }

    bool isRunning()         const { return commandState == CommandState::IN_PROGRESS; }
    bool isCommandValid()    const { return commandState == CommandState::COMPLETE; }
    bool isCommandInvalid()  const { return commandState == CommandState::INVALID; }
    bool isHelpActive()      const { return inputMode == InputMode::HELP || inputMode == InputMode::TAB; }
    bool isLineActive()      const { return inputMode == InputMode::LINE; }
    bool isMatchSuccessful() const { return matchState == MatchState::EXACT || matchState == MatchState::PATTERN; }
    bool isPatternMatching() const { return matchState == MatchState::PATTERN; }

    bool isValidDir(const nlohmann::ordered_json* dir) const
    {
        return dir && dir->is_object() && dir->contains(CLI_JSON_SUBCOMMAND_ARRAY);
    }

    /// @brief Splits a line into tokens, stopping at a trailing `?` or tab.
    std::vector<std::string_view> tokenize(const std::string& line);

    /// @brief Widens `token` to cover the rest of `line`, for LINE-pattern capture.
    void extendLineToken(const std::string& line, std::string_view& token, bool help);

    /**
     * @brief Returns the commands selectable at the current tree level.
     *
     * Expands variable and recursive nodes, filters by negate/default visibility,
     * marks exact and partial matches against `userInput`, and descends
     * `currentDirectory` when exactly one candidate matches. Also updates
     * `commandState`, `eoc`, and `err` as a side effect.
     *
     * @param userInput    The token being matched at this level.
     * @param prevCommands Candidates from the previous level, for property lookup.
     * @return Candidate list for help/completion display.
     */
    std::vector<Com> availableAt(std::string_view userInput, std::vector<Com>& prevCommands);

    /**
     * @brief Tests `input` against a placeholder `pattern` and records the result.
     *
     * On success sets `currentPattern` and flips `matchState` to PATTERN, and for
     * LINE placeholders switches `inputMode` to LINE so the remainder of the line
     * is captured verbatim.
     */
    bool matchPattern(std::string_view input, const std::string& pattern);

private:

    // AVAILABLE AT HELPERS

    static bool hasProp(const Com* cmd, const std::string& a, const std::string& aAll);
    static bool shouldHide(const nlohmann::ordered_json& cmd, bool negate);
    static Com buildCom(const nlohmann::ordered_json& cmd, std::string_view raw);
    void buildViews(std::vector<NodeView>& views);

    // MATCH PATTERN HELPERS

    static bool matchWord(std::string_view input, std::string_view previousMatch);
};
}

#endif // COMMAND_TREE_H
