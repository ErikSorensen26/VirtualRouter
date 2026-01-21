// CliEngine.h

#ifndef CLI_ENGINE_H
#define CLI_ENGINE_H

#include <string>
#include <vector>
#include <json.hpp>
#include <Time.h>
#include <condition_variable>
#include "Configs.h"

enum class InterfaceType: uint8_t;
enum class CliMode;
class Interface;
class CliSession;
class CommandProcessor;
class IConsole;
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
enum RoutingMode
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
 * - The global command tree (parsed from JSON / CBOR at runtime)
 * - The configuration schema used for validation and mode enforcement
 * - The lifecycle of all active CLI sessions (`CliSession`)
 * - Integration points with the router’s persistent configuration and boot-up sequence
 *
 * ### Architectural Role
 * - Acts as the authoritative root of all CLI structural information.
 * - Loads, validates, transforms, and publishes the command hierarchy for runtime use.
 * - Serves as the synchronization point between the configuration subsystem (`Configs`),
 *   routing instances, and CLI sessions.
 * - Provides helpers to determine legal command paths, numeric parsing, directory validity,
 *   and masking behavior for partial commands.
 *
 * ### Memory & Ownership Model
 * - Owns the dynamically allocated `CliSession` objects and is responsible for destroying them.
 * - Owns the JSON command tree and configuration schema for the lifetime of the process.
 * - Uses `Configs` (its base class) to manage persistent configuration lifecycle and recovery.
 *
 * ### Concurrency Model
 * - CLI sessions may perform reads of the command tree concurrently; the tree itself is loaded
 *   once at initialization and is not mutated thereafter (except during early initialization).
 * - `recoverState()` executes sequentially during engine initialization; sessions are not active yet.
 * - Condition variables (`stateCondition`) support future synchronization between CLI and other
 *   subsystems, though in the current implementation they are used minimally.
 *
 * ### Interaction With Subsystems
 * - Interfaces with router boot logic through `StartupFiles` and persistent configuration restore.
 * - Provides session instances which interact with `CommandProcessor` and `Mode` subsystems.
 * - Uses JSON-based command definitions to enforce grammar, help generation, and mode transitions.
 *
 * ### Invariants
 * - `commandTree` must be a valid JSON object containing at minimum the variables block and
 *   command definitions.
 * - Schema objects, when present, must be valid JSON maps.
 * - All sessions created must be tracked in `sessions` and destroyed inside `clearSessions()` or
 *   the destructor.
 *
 * ### Performance Notes
 * - JSON parsing (CBOR or text JSON) is optimized for boot; all heavy computation happens once.
 * - Session creation is lightweight and context-bound; parsing depth/structure is offloaded to
 *   `CommandProcessor` and runtime modes.
 */
class CliEngine : public Configs
{
public:
    friend class Internal_CliTest;
    Com errorCommand;           ///< Represents a sentinel command used when parsing fails or input is malformed.
    Com carriageReturnCommand;  ///< Represents a carriage return '<cr>>' used by the CLI engine as a structural placeholder.

    static std::string defaultMode; ///< Default operational mode for newly created sessions (typically user EXEC).

    const std::vector<std::string> globalCommandList{"?", "vk_tab"}; ///< List of globally valid commands independent of mode.
    size_t paginationCount = 10; ///< Maximum number of entries displayed before pagination is triggered.
    DoTime timeManager;          ///< Shared time-management utility used for timestamping or delayed operations.

    std::vector<CliSession*> sessions; ///< All active CLI session owned by the engine.

    Global& global; ///< Reference to the system wide global instance.

    /**
     * @brief Constructs a CLI engine bound to a given router global context.
     *
     * This constructor:
     * - Performs the base `Configs` initialization.
     * - Registers a default routing instance.
     * - Does **not** immediately load configuration or command trees.
     *
     * @param global Reference to the global router manager and root routing instance registry.
     * @param stfs   Startup file descriptors required for configuration/bootstrap.
     * @param test   If true, bypasses certain initialization and load steps for deterministic testing.
     */
    CliEngine(Global& global, const StartupFiles& stfs, bool test = false);

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
    CliEngine(Global& global, const StartupFiles& stfs, IFileSystem* fs, bool test = false);

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

    // INITIALIZATION LOGIC

    /**
     * @brief Performs complete initialization of CLI engine resources.
     *
     * Responsibilities:
     * - Initializes configuration subsystems via `Configs::initConfigs()`
     * - Loads command-tree JSON (CBOR or text) and writes CBOR cache
     * - Loads configuration schema JSON
     * - Calls `initTree()` to finalize variable interpolation
     * - Calls `recoverState()` to rebuild CLI-driven configuration from history
     *
     * Preconditions:
     * - Files referenced in `StartupFiles` should be accessible.
     */
    void initEngine(const StartupFiles& stfs);

    /**
     * @brief Finalizes the dynamically loaded command tree.
     *
     * This function adjusts variable-backed CLI arguments (such as interface indices)
     * using the current hardware manager’s physical interface inventory.
     *
     * The function modifies commandTree in-place during early initialization.
     */
    void initTree();

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
    CliSession* createSession(IConsole* console);

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
    const nlohmann::ordered_json& getCommandTree() const;

    /**
     * @brief Returns the configuration schema.
     *
     * This provides the structural validation layer for configuration modes.
     *
     * @return Const reference to the configuration schema JSON.
     */
    const nlohmann::ordered_json& getConfigSchema() const;

    /**
     * @brief Shorthand accessor for commandTree.
     *
     * Provided for convenience and compatibility with existing code.
     *
     * @return Const reference to commandTree.
     */
    const nlohmann::ordered_json& getCommandTree() { return commandTree; }

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
     * @brief Validates whether a JSON object represents a legal command directory.
     *
     * A valid command directory must contain a `"subcommands"` key. This check allows
     * the command processor to determine whether a token should descend into a subtree.
     *
     * @param directory Pointer to the JSON object.
     * @return True if valid, false otherwise.
     */
    bool isValidCommandDirectory(nlohmann::ordered_json* directory);

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
    std::string maskInput(const std::string& prefix, std::string original);

    /**
     * @brief Retrieves the command tree;
     */
    const nlohmann::ordered_json& getCommandTree() { return commandTree; }

    const std::vector<std::string> globalCommandList{"?", "vk_tab"}; ///< List of global commands
    size_t paginationCount = 10; ///< Pagination count for command help
    DoTime timeManager; ///< Manages time-related functionality
    static CliMode defaultMode;
    void clearSessions();

    std::vector<CliSession*> sessions;

    Global& global;

private:

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

    nlohmann::ordered_json commandTree; ///< Loaded command tree (CBOR or JSON) describing full CLI grammar.
    std::condition_variable stateCondition; ///< Condition variable reserved for future synchronization.
};

#endif // CLI_ENGINE_H
