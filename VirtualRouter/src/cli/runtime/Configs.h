/**
 * @file Configs.h
 * @brief CLI configuration management: file paths, startup, and schema loading.
 *
 * Manages CLI configuration state including command tree loading, configuration
 * schema validation, startup file paths, and persistent configuration I/O.
 */

#ifndef CONFIGS_H
#define CONFIGS_H

#include <fstream>
#include <string>
#include <cstdio>
#include <cmath>
#include <cstdio>

#include <curses.h> 
#include <unistd.h>   
#include <termios.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <pugixml.hpp>
#include <json.hpp>
#include <hardware/HardwareManager.h>
#include <Mock.hpp>

#define COMMAND_TREE_BIN "./configs/Commands.bin"
#define COMMAND_TREE "./configs/Commands.json"
#define CONFIG_SCHEMA "./configs/ConfigSchema.json"
#define HW_CONFIG_FILE "./configs/Configs.json"
#define ROUTER_CONFIG_FILE "./dir/configs.json"
#define MODE_KEY "commands"

using json = nlohmann::ordered_json;

namespace core { class Global; }
namespace hardware { class HardwareManager; }

namespace cli
{
struct ModeConfig;

/**
 * @brief Defines filesystem paths for all boot-time configuration inputs.
 *
 * This struct models how a router determines its initial configuration state.
 * Paths supplied here direct the system toward:
 *
 * ### Configuration Loading Pipeline
 * - The startup configuration (`startupFile`) used during boot.
 * - The persistent router config (`routerConfigFile`) where `write memory` or equivalent
 *   operations store the canonical running configuration.
 * - The hardware configuration (`hwConfigFile`) consumed by `HardwareManager` to construct
 *   platform-specific physical/virtual interfaces, line cards, and platform behavioral modules.
 *
 * Startup files imply no dynamic ownership; all paths remain valid for the lifetime of the calling context.
 */
struct StartupFiles
{
    std::string startupFile = ROUTER_CONFIG_FILE;       ///< Startup configuration path.
    std::string routerConfigFile = ROUTER_CONFIG_FILE;  ///< Persistent router configuration path.
    std::string hwConfigFile = HW_CONFIG_FILE;          ///< Hardware model configuration path.
};

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
    std::string name;                ///< Human-readable command keyword.
    std::string description;         ///< Description of semantic behavior.
    std::vector<std::string> properties; ///< Arbitrary property labels (e.g., feature flags).

    /**
     * @brief Enumerates supported levels of functionality for a given command.
     * @ingroup CLI_RUNTIME
     *
     * Used by platform-capability systems to mark incomplete or partially-implemented commands.
     */
    enum class Support
    {
        SUPPORTED,      ///< Fully implemented.
        PARTIAL,        ///< Implemented with limitations.
        NO_SUPPORT      ///< Disabled or unavailable.
    };
    Support support = Support::NO_SUPPORT; ///< Implementation capability indicator.
};

/**
 * @brief Encapsulates local computations for volatile parameter normalization.
 *
 * Many router commands include volatile operands (IP addresses, masks, wildcard identifiers,
 * interface-unique strings, etc.). These are not stable keys and must be normalized before
 * insertion into the configuration tree.
 *
 * ### Architectural Role
 * - Provides classification helpers for volatile values.
 * - Used primarily during configuration building and recovery.
 *
 * ### Memory & Ownership Model
 * - Contains POD values only.
 * - No dynamic allocation; lifetime is stack-managed.
 *
 * ### Threading Model
 * - Not thread-safe; intended for single-threaded CLI and parsing contexts.
 *
 * ### Invariants
 * - Returned strings must remain deterministic for identical input sequences.
 * - No external state beyond stored POD members.
 */
class volatileValueUsage 
{
public:

    /**
     * @brief Returns stringified numeric value.
     * @ingroup CLI_RUNTIME
     */
    std::string getValue() { return std::to_string(value); }

    /**
     * @brief Returns stringified IPv4 address value.
     */
    std::string getIp() { return std::to_string(ip); }

    /**
     * @brief Returns stringified IPv6 value.
     */
    std::string getIpv6() { return std::to_string(ipv6); }

    /**
     * @brief Returns stringified subnet mask value.
     */
    std::string getSubnet() { return std::to_string(subnet); }

    /**
     * @brief Returns stringified MAC address value.
     */
    std::string getMac() { return std::to_string(mac); }

    /**
     * @brief Returns stringified XYZ coordinate.
     */
    std::string getXYZ() { return std::to_string(xyz); }

    /**
     * @brief Returns stringified volatile identifier.
     */
    std::string getID() { return std::to_string(id); }

private:
    unsigned int value = 0;   ///< Generic volatile scalar.
    unsigned int ip = 0;      ///< IPv4 volatile value normalized as integer.
    unsigned int ipv6 = 0;    ///< IPv6 volatile placeholder.
    unsigned int subnet = 0;  ///< Subnet mask volatile value.
    unsigned int mac = 0;     ///< MAC volatile value.
    unsigned int xyz = 0;     ///< Coordinate volatile value.
    unsigned int id = 0;      ///< Generic ID value.
};

/**
 * @brief Concrete filesystem implementation used by the router process.
 *
 * Backed by POSIX operations including `open`, `mmap`, and `std::ofstream`.
 *
 * ### Memory & Ownership Model
 * - `readFile` uses `mmap` for large-file efficiency.
 * - Caller owns returned string content.
 *
 * ### Concurrency Model
 * - Not thread-safe; assumes serialization by the caller.
 */
class FileSystem
{
public:
    MOCK ~FileSystem() = default;

    MOCK bool readFile(const std::string& path, std::string& content)
    {
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;

        struct stat st;
        fstat(fd, &st);

        void* data = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) return false;

        content.assign((char*)data, st.st_size);
        munmap(data, st.st_size);
        close(fd);
        return true;
    }

    MOCK bool writeFile(const std::string& path, const std::string& content)
    {
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file.is_open())
        {
            return false; // Cannot open the file for writing
        }

        file << content;
        file.close();
        return true;
    }

    MOCK bool fileExists(const std::string& path)
    {
        return std::filesystem::exists(path);
    }

    MOCK void removeFile(const std::string& path)
    {

    }
};

/**
 * @brief Core subsystem responsible for building, validating, ordering, saving,
 * @ingroup CLI_RUNTIME
 * and recovering the router’s hierarchical configuration tree.
 *
 * The Configs class is the authoritative owner of the router configuration model.
 * It consumes schema definitions, JSON-based configuration documents, hardware
 * descriptions, and CLI-driven command mutations to maintain a consistent,
 * mode-sensitive representation of the router state.
 *
 * ### Architectural Responsibilities
 * - Serves as the central configuration authority for all CLI modes.
 * - Owns the root JSON tree representing active configuration.
 * - Applies schema-defined ordering rules to ensure deterministic serialization.
 * - Manages volatile command operands (e.g., IPs, masks, IDs) via normalization.
 * - Interacts with `HardwareManager` to bootstrap hardware topology.
 * - Provides recovery paths by reconstructing CLI commands from stored JSON.
 *
 * ### Concurrency Model
 * - Designed for single-threaded CLI workflows.
 * - Not reentrant; callers must externally synchronize if shared.
 *
 * ### Memory & Ownership Model
 * - Owns `hwManager` (allocated dynamically in `initConfigs`).
 * - Does not own the supplied `FileSystem`; caller must manage its lifetime.
 * - JSON nodes (`root`, `configSchema`) persist for the entire router session.
 *
 * ### Mode-Machine Integration
 * - Writes and interprets commands based on the caller-supplied `ModeConfig`.
 * - Creates nested mode nodes under `commands` to represent router configuration contexts.
 *
 * ### Performance Notes
 * - Uses ordered JSON containers to ensure deterministic flush ordering.
 * - Minimizes memory churn during incremental command writes.
 */
class Configs 
{
public:
    friend class Internal_ConfigTest;

    /**
     * @brief Constructs a Configs controller bound to a filesystem implementation.
     *
     * ### Behavior
     * - Does not load configuration at construction.
     * - Defers hardware bootstrap until `initConfigs`.
     *
     * ### Ownership
     * - Does not take ownership of `fileSystem`; caller must manage lifetime.
     */
    Configs(FileSystem& fileSystem);

    /**
     * @brief Initializes all configuration state from startup files and hardware descriptors.
     *
     * ### Behavior
     * - Resets the in-memory config tree.
     * - Spawns `HardwareManager` using platform configuration.
     * - Loads and parses JSON router configuration if present.
     * - Constructs `configSchema` when absent.
     *
     * ### Preconditions
     * - `fileSystem` must be valid.
     *
     * ### Ownership
     * - Allocates and owns `hwManager`.
     *
     * @param stfs File paths describing startup and hardware configuration sources.
     * @param enableDummies Enables simulated hardware components for test environments.
     */
    void initConfigs(const StartupFiles& stfs, bool enableDummies = true);

    /**
     * @brief Recovers routed CLI commands by traversing the stored configuration model.
     *
     * This is the inverse of `saveCommand()`: it reconstructs canonical CLI expressions
     * from the JSON hierarchy and the schema ordering rules.
     *
     * ### Architectural Context
     * - Used during boot to rebuild candidate running-config state.
     * - Supports rollback, provisioning, and audit tools.
     *
     * @param json Optional JSON root for recovery. If null, uses internal `root`.
     * @return Ordered list of canonical CLI commands.
     */
    std::vector<std::string> recoverConfigs(nlohmann::ordered_json* json = nullptr);

    /**
     * @brief Recursively walks the configuration tree to extract CLI command sequences.
     *
     * ### Behavior
     * - Interprets volatile values.
     * - Detects mode context branches (`commands`).
     * - Appends fully-formed commands at recursion terminals.
     *
     * ### Invariants
     * - Must not mutate configuration state.
     *
     * @param currentNode JSON node under traversal.
     * @param command Accumulated command tokens.
     * @param commandList Receiver for output commands.
     */
    void processConfigs(nlohmann::ordered_json* currentNode, std::vector<std::string> command, std::vector<std::string>& commandList);

    /**
     * @brief Applies a CLI command into the configuration tree with schema-aware ordering
     * and optional mode transitions.
     *
     * ### Behavior
     * - Normalizes volatile operands.
     * - Creates missing nodes, respecting schema-defined lexical order.
     * - Detects duplicate entries for list-style commands.
     * - Updates `modeConfig.configNode` when entering or exiting mode contexts.
     *
     * ### Mode Interaction
     * - Disallowed in non-configuration modes (userExec / privilegedExec).
     * - Optionally pushes/pops mode history.
     *
     * ### Preconditions
     * - `modeConfig.configNode` must reference a valid JSON node.
     *
     * @return True if the command was successfully applied.
     */
    bool saveCommand(std::vector<std::string>& oldCommand,
                     std::vector<std::string>& command,
                     ModeConfig& modeConfig,
                     bool changeMode,
                     bool exitMode,
                     bool isList);

    /**
     * @brief Inserts a command (main or subcommand) into its parent node while honoring
     * schema-specified ordering rules.
     *
     * ### Architectural Role
     * - Ensures deterministic serialization regardless of insertion sequence.
     * - Defines CLI recovery determinism across versions and platforms.
     *
     * @param parentNode JSON node into which keys are inserted.
     * @param modeConfig Mode configuration containing active schema.
     * @param mainCommand Main command keyword.
     * @param subCommand Optional subordinate command keyword.
     * @param isListed Whether the command represents a list-style node.
     */
    void insertOrdered(nlohmann::ordered_json* parentNode,
                       ModeConfig& modeConfig,
                       const std::string& mainCommand,
                       const std::string& subCommand = "",
                       bool isListed = false);

    /**
     * @brief Serializes in-memory configuration to disk as formatted JSON.
     *
     * ### Behavior
     * - Produces human-readable configuration captures.
     * - Overwrites existing startup config.
     *
     * @return Success indicator from filesystem implementation.
     */
    bool saveConfig();

    /**
     * @brief Removes a configuration entry from the JSON tree.
     *
     * Although the implementation currently acts as a stub, its architectural role is:
     *
     * ### Responsibilities
     * - Perform inverse of `saveCommand`.
     * - Support `no ...` style CLI semantics.
     * - Maintain schema ordering after removal.
     *
     * @return True if deletion succeeded.
     */
    bool deleteConfig(ModeConfig& modeConfig,
                      std::vector<std::string>& oldCommand,
                      std::vector<std::string>& command,
                      bool isList);

    /**
     * @brief Identifies whether a command token represents a volatile operand.
     *
     * Volatile operands cannot be used as stable JSON keys and require normalization:
     * IPv4, IPv6, masks, wildcard IDs, MACs, generic `<val-range>` parameters, etc.
     *
     * @param str Candidate command token.
     * @return True if the token must be normalized.
     */
    bool isVolatile(const std::string& str);

    /**
     * @brief Converts a token sequence into a canonical space-delimited CLI expression.
     *
     * Used during recovery and as part of the debugging pipeline.
     */
    std::string joinCommand(const std::vector<std::string>& command);

    /**
     * @brief Resolves a normalized volatile key name based on current JSON siblings.
     *
     * ### Behavior
     * - Guarantees uniqueness by appending numeric suffixes.
     * - Determines semantic class (IP, mask, wildcard, ID, etc.).
     *
     * @return Normalized volatile key safe for use as a JSON object key.
     */
    std::string getVolatileValue(std::string& type,
                                 std::string& value,
                                 nlohmann::ordered_json& currentJson);

    /**
     * @brief Resolves volatile key names using already-observed volatile operands.
     *
     * Used during deduplication detection in list-style configuration modes.
     *
     * @return Stable JSON key for volatile operand.
     */
    std::string getVolatileValue(std::string& type,
                                 std::string value,
                                 std::vector<std::string> volatileValues);

    /**
     * @brief Computes the base volatile key type for a token (e.g., ip, mask, wildcard).
     *
     * Does not guarantee uniqueness—that is handled by the overloads of `getVolatileValue`.
     */
    std::string getVolatileValueHelper(std::string& command, std::string& com);

    /**
     * @brief Selects an alternate configuration schema based on active CLI mode.
     *
     * This drives mode-specific command ordering (e.g., interface mode, routing-protocol mode).
     *
     * @param mode Target mode name.
     */
    void setSchemaMode(const std::string& mode);

    /**
     * @brief Emits the current configuration tree for diagnostic purposes.
     *
     * Typically suppressed in production; used heavily during development and automated tests.
     */
    void printConfig();

    std::string routerConfigFilename{};     ///< Current persistent configuration file path.
    json root;                              ///< Root of hierarchical router configuration.
    json configSchema;                      ///< Active schema controlling command ordering.
    FileSystem& fileSystem;                ///< Filesystem interface used for persistence.
    hardware::HardwareManager hwManager;    ///< Hardware abstraction subsystem.
	
private:
    std::vector<std::string> volatileInputs{
        "WORD", "LINE", "A.B.C.D", "X:X:X:X::X",
        "X:X:X:X::X/<0-128>", "H.H.H", "x/y/z"
    }; ///< Known volatile patterns.

    std::vector<std::string> inputs{
        "ip", "subnet", "id", "value", "ipv6", "mac"
    }; ///< Stable volatile classifier names.

    std::vector<std::string> recover; ///< Holds recovered CLI commands.
};
}

#endif // CONFIGS_H
