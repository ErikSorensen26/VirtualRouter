// CliEngine.h

#ifndef CLI_ENGINE_H
#define CLI_ENGINE_H

#include <string>
#include <vector>
#include <json.hpp>
#include <Time.h>
#include "Configs.h"
#include "Interface.h"

class CliSession;
class CommandProcessor;

/**
 * @enum RoutingMode
 * @brief Enumerates the various routing protocols supported by the CLI.
 */
enum RoutingMode
{
    BGP,            ///< Border Gateway Protocol
    EIGRP_CLASSIC,  ///< Enhanced Interior Gateway Routing Protocol Classic
    EIGRP_NAMED,    ///< Enhanced Interior Gateway Routing Protocol Named
    OSPF,           ///< Open Shortest Path First
    RIP             ///< Routing Information Protocol
};

/**
 * @class CliEngine
 * @brief Manages the shared command tree and configuration schema for CLI sessions.
 */
class CliEngine : public Configs
{
public:

    Com errorCommand;           ///< Represents an error command
    Com carriageReturnCommand;  ///< Represents a carriage return command

    /**
     * @brief Constructor. Initializes the engine.
     */
    CliEngine();

    /**
     * @brief Destructor.
     */
    ~CliEngine();

    /**
     * @brief Initialization function for the CliEngine class
     *
     * This function fully initialized this class by setting the configuration files
     */
    void initEngine();

    /**
     * @brief Creates a new CLI session.
     * @param debug Whether to enable debug mode for the session.
     * @return Shared pointer to the created session.
     */
    CliSession* createSession(bool debug = false);

    /**
     * @brief Gets the loaded command tree.
     * @return JSON object representing the command tree.
     */
    const nlohmann::json& getCommandTree() const;

    /**
     * @brief Gets the loaded configuration schema.
     * @return JSON object representing the config schema.
     */
    const nlohmann::json& getConfigSchema() const;

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
    bool isValidCommandDirectory(nlohmann::json* directory);

    /**
     * @brief Masks the prefix over the original.
     * 
     * Takes the original string and copies the prefix over the beginning of it.
     * 
     * @param prefix Reference to the prefix string to be masked.
     * @param original Original string to by copied over.
     * @return std::string The masked string.
     */
    std::string maskInput(const std::string& prefix, std::string original);

    /**
     * @brief Determines the interface type based on a string identifier.
     *
     * Maps a string representing an interface type to its corresponding enum value.
     * Logs a warning if the interface type is undefined.
     *
     * @param type The string identifier of the interface type.
     * @return InterfaceType The corresponding enum value of the interface type.
     */
    InterfaceType getInterfaceType(const std::string& type);

    /**
     * @brief Retrieves the command tree;
     */
    const nlohmann::json& getCommandTree() { return commandTree; }

    const std::vector<std::string> globalCommandList{"?", "vk_tab"}; ///< List of global commands
    size_t paginationCount = 10; ///< Pagination count for command help
    DoTime timeManager; ///< Manages time-related functionality
    static std::string defaultMode;

    std::vector<CliSession*> sessions;
private:

    /**
     * @brief Reverses the effects of a processed command.
     *
     * Specifically used for handling "no" commands that negate or undo previous configurations.
     *
     * @param command The command string to undo
     */
    void undoCommand(std::string& command, std::string executionMode);

    /**
     * @brief Recovers the CLI state from saved configurations.
     *
     * Retrieves a list of previously executed commands from persistent storage (e.g., XML files),
     * executes them to restore the CLI's state, and introduces delays for stability.
     */
    void recoverState();

    nlohmann::json commandTree; ///< JSON structure holding the command hierarchy.
    std::condition_variable stateCondition; ///< Condition variabel for thread synchronization.

};

#endif // CLI_ENGINE_H
