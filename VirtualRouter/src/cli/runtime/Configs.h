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
    std::vector<std::string> recover; ///< Holds recovered CLI commands.
};
}

#endif // CONFIGS_H
