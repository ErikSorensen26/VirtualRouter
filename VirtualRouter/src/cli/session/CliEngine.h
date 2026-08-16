/**
 * @file CliEngine.h
 * @brief CLI engine: centralized command-tree, session, and configuration management.
 *
 * Owns and manages the global command tree, configuration schema, active sessions,
 * and integration with the router's persistent state. Serves as the top-level
 * orchestration layer for all CLI subsystems.
 */

/**
 * @defgroup CLI_RUNTIME CLI Runtime
 * @ingroup CLI
 * @brief Session management, engine, console, and configuration I/O.
 */

#ifndef CLI_ENGINE_H
#define CLI_ENGINE_H

#include <string>
#include <vector>
#include <Json.hpp>
#include <Time.h>
#include <condition_variable>
#include <Mock.hpp>
#include "Configs.h"
#include "cli/tree/CommandTree.h"
#include "cli/terminal/ConsoleController.hpp"

#define COMMAND_TREE_BIN "./configs/Commands.bin"
#define COMMAND_TREE "../VirtualRouter/commands"

namespace interface { class Interface; enum class InterfaceType : uint8_t; }
namespace config { class GlobalRegistry; }

static std::string_view CARRIAGE_RETURN = "<cr>";
static constexpr std::string_view DO_EXEC_KEYWORD = "do-exec";

namespace cli
{
class CliSession;
class CommandProcessor;
class ConsoleController;
struct ModeConfig;

/**
 * @enum RoutingMode
 * @brief Identifies all routing-protocol domains that the CLI can expose for configuration.
 *
 * This enumeration is used by higher-level CLI subsystems and the routing infrastructure
 * to determine which semantic domain a command applies to. Each value corresponds to a
 * distinct protocol with its own configuration schema, operational state, and internal
 * constraints.
 *
 * The enumeration is intentionally flat; each protocol manages its own internal mode
 * transitions using the CLI’s mode engine. These values are used primarily to gate
 * protocol-specific configuration under the correct CLI subtrees, as the command tree
 * and configuration schema are both generated dynamically at runtime.
 *
 * All routing modes listed here correspond to subsystems maintained within the virtual
 * router. Configuration for each mode typically affects persistent state, routing
 * instances, interface roles, and protocol adjacency formation.
 */
enum class RoutingMode
{
    BGP,            ///< Border Gateway Protocol.
    EIGRP_CLASSIC,  ///< Enhanced Interior Gateway Routing Protocol - Classic.
    EIGRP_NAMED,    ///< Enhanced Interior Gateway Routing Protocol - Named.
    OSPF,           ///< Open Shortest Path First (link-state routing).
    RIP             ///< Routing Information Protocol (distance-vector).
};

/**
 * @class CliEngine
 * @brief Centralized command-tree manager and multi-session orchestration layer for the CLI subsystem.
 *
 * The `CliEngine` owns and manages:
 * - The global command tree (compiled from grammar JSON into a binary format and mmap’d at runtime)
 * - The configuration schema used for validation and mode enforcement
 * - The lifecycle of all active CLI sessions (`CliSession`)
 * - Integration points with the router’s persistent configuration and boot-up sequence
 *
 * ### Architectural Role
 * - Acts as the authoritative root of all CLI structural information.
 * - Loads and validates the pre-compiled command tree; regenerates from source if stale or invalid.
 * - Serves as the synchronization point between the configuration subsystem (`Configs`),
 *   routing instances, and CLI sessions.
 * - Provides helpers to determine legal command paths, numeric parsing, directory validity,
 *   and masking behavior for partial commands.
 *
 * ### Memory & Ownership Model
 * - Owns the dynamically allocated `CliSession` objects and is responsible for destroying them.
 * - Owns a memory-mapped `CommandTree` (compiled binary from grammar sources) for the lifetime
 *   of the process. The tree is immutable after initialization and safe for concurrent reads.
 * - Uses `Configs` (its base class) to manage persistent configuration lifecycle and recovery.
 *
 * ### Concurrency Model
 * - CLI sessions may perform reads of the command tree concurrently; the tree is loaded once
 *   at initialization and is not mutated thereafter.
 * - `recoverState()` executes sequentially during engine initialization; sessions are not active yet.
 * - Condition variables (`stateCondition`) support future synchronization between CLI and other
 *   subsystems, though in the current implementation they are used minimally.
 *
 * ### Interaction With Subsystems
 * - Interfaces with router boot logic through `StartupFiles` and persistent configuration restore.
 * - Provides session instances which interact with `CommandProcessor` and `Mode` subsystems.
 * - Uses the compiled command tree to enforce grammar, help generation, and mode transitions.
 *
 * ### Invariants
 * - `commandTree` must be a valid, memory-mapped command tree built from grammar JSON sources.
 * - The tree’s content hash must match the grammar source hash, or regeneration occurs automatically.
 * - All sessions created must be tracked in `sessions` and destroyed inside `clearSessions()` or
 *   the destructor.
 *
 * ### Performance Notes
 * - Grammar compilation happens once during tree setup and is cached in `Commands.bin`.
 * - The tree is memory-mapped for fast startup; staleness is detected via content hashing.
 * - Session creation is lightweight and context-bound; tree traversal is offloaded to
 *   `CommandProcessor` and runtime modes.
 */
class CliEngine : public Configs
{
public:
    friend class Internal_CliTest;

    static std::string defaultMode; ///< Default operational mode for newly created sessions (typically user EXEC).

    size_t paginationCount = 10; ///< Maximum number of entries displayed before pagination is triggered.
    ::utils::DoTime timeKeeper;          ///< Shared time-management utility used for timestamping or delayed operations.

    std::vector<CliSession*> sessions; ///< All active CLI session owned by the engine.

    core::Global& global; ///< Reference to the system wide global instance.

    /**
     * @brief Constructs the CLI engine with a custom filesystem backend.
     *
     * This overload allows injection of virtual/redirected file systems for testing,
     * embedded deployments, or alternate persistence layers.
     *
     * If `test` is false, the function performs full engine initialization:
     * - Loads startup configs
     * - Parses JSON command tree
     * - Loads schema definitions
     * - Restores configuration state from persistent history
     *
     * @param global Reference to the global router manager/root routing instance registry.
     * @param stfs   Startup file collection describing command tree, schema, and recovery files.
     * @param fs     Custom file-system interface used for all CLI-related persistence.
     * @param test   Disable initialization logic when true.
     */
    CliEngine(core::Global& global, const StartupFiles& stfs, FileSystem& fs, bool test = false);

    /**
     * @brief Destroys the CLI engine and all active sessions.
     *
     * This destructor:
     * - Iterates through all owned `CliSession*` instances
     * - Safely deletes them
     * - Clears the session list
     *
     * All JSON structures, configuration schema, and global references remain valid until the
     * surrounding router context is torn down.
     */
    ~CliEngine();

    // SESSION MANAGEMENT

    /**
     * @brief Creates a new CLI session using the engine's default console implementation.
     *
     * The returned session is owned by the engine and returned as a raw pointer. The caller
     * should not delete it; destruction occurs in the engine’s destructor or `clearSessions()`.
     *
     * @param debug When true, enables debug behavior inside the session.
     * @return Pointer to the newly created session.
     */
    CliSession* createSession(bool debug = false);

    /**
     * @brief Creates a new CLI session using an externally supplied console implementation.
     *
     * This allows embedding environments and automated test harnesses to supply their own
     * input/output backend (terminal emulator, socket, file pipe, etc.).
     *
     * Ownership of the session remains with the engine; ownership of the console remains
     * with the caller.
     *
     * @param console External console handler to attach to the CLI session.
     * @return Pointer to the newly created session.
     */
    CliSession* createSession(ConsoleController& console);

    /**
     * @brief Destroys all active CLI sessions and clears the internal registry.
     *
     * This is typically used during router shutdown, topology reload, or test cleanup.
     */
    void clearSessions();

    // ACCESSORS

    /**
     * @brief Returns the global command tree used by all CLI sessions.
     *
     * This JSON object is immutable after initialization and therefore safe to return by const reference.
     *
     * @return The loaded command map.
     */
    tree::CommandTree& getCommandTree() const { return commandTree; }

    // UTILITIES

    /**
     * @brief Checks whether a given string represents a numeric integer literal.
     *
     * Accepts:
     * - Optional leading '+' or '-'
     * - Digits thereafter
     *
     * Used by CLI argument parsing to differentiate numeric tokens from keywords.
     *
     * @param input Input string.
     * @return True if numeric, false otherwise.
     */
    bool isNumeric(const std::string& input);

    /**
     * @brief Masks the prefix string over the original input.
     *
     * This operation is used by the CLI editor to preserve user input while substituting
     * auto-completed prefixes or template expansions.
     *
     * If the prefix is longer than the original, the original is returned unchanged.
     *
     * @param prefix   The prefix to apply.
     * @param original The original input string.
     * @return New string with prefix applied over the first N characters.
     */
    std::string& maskInput(std::string_view prefix, std::string& original);

private:
    ConsoleController controller;


    /**
     * @brief Reverses previously applied configuration commands.
     *
     * Used for `no <command>` semantics:
     * - Parses the previously executed command
     * - Generates the appropriate negated structure
     * - Replays it to revert configuration state
     *
     * @param command       The command string to undo.
     * @param executionMode The mode in which the command should be interpreted.
     */
    void undoCommand(std::string& command, std::string executionMode);

    /**
     * @brief Restores CLI-based configuration from persistent history.
     *
     * Behavior inferred from implementation:
     * - Retrieves saved command history from `Configs::recoverConfigs()`
     * - Creates a temporary CLI session
     * - Forces the session into global configuration mode
     * - Executes each saved command sequentially
     * - Applies throttling delays to avoid race conditions in early boot
     *
     * This function is strictly single-threaded and intended only for initialization.
     */
    void recoverState();

    mutable tree::CommandTree commandTree; ///< Loaded command tree describing the full CLI grammar.
    std::condition_variable stateCondition; ///< Condition variable reserved for future synchronization.
};
}

#endif // CLI_ENGINE_H
