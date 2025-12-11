// Mode.hpp

#ifndef MODE_HPP
#define MODE_HPP

#include <string_view>
#include <array>
#include <optional>
#include <json.hpp>
#include <ContextBase.hpp>

class CliSession;

#define CLI_MODE_TABLE \
    X(UserExec,                        ">") \
    X(PrivilegedExec,                  "#") \
    X(GlobalConfiguration,             "(config)#") \
    X(DhcpGlobalOptions,               "(config-dhcp-global-options)#") \
    X(DhcpConfig,                      "(config-dhcp)#") \
    X(Dhcpv6Config,                    "(config-dhcpv6)#") \
    X(FlowExporter,                    "(config-flow-exporter)#") \
    X(FlowMonitor,                     "(config-flow-moniter)#") \
    X(FlowRecord,                      "(config-flow-record)#") \
    X(Interface,                       "(config-if)#") \
    X(ClassMap,                        "(config-cmap)#") \
    X(Dhcp,                            "(config-dhcp)#") \
    X(ExtendedACL,                     "(config-ext-nacl)#") \
    X(StandardACL,                     "(config-std-nacl)#") \
    X(PolicyMap,                       "(config-pmap)#") \
    X(Routing,                         "(config-router)#") \
    X(RoutingV6,                       "(config-rtr)#") \
    X(RouterAddressFamily,             "(config-router-af)#") \
    X(RouterAddressFamilyInterface,    "(config-router-af-interface)#") \
    X(RouterAddressFamilyTopology,     "(config-router-af-topology)#")

/**
 * @struct Mode
 * @brief Represents various operational modes of the terminal with corresponding command-line prompts.
 */
enum class CliMode
{
#define X(name, str) name,
    CLI_MODE_TABLE
#undef X
    Count
};

static constexpr std::array<std::string_view,
    static_cast<size_t>(CliMode::Count)>
CliModeStrings =
{
#define X(name, str) str,
    CLI_MODE_TABLE
#undef X
};

constexpr std::string_view getModePrompt(CliMode mode)
{
    return CliModeStrings[static_cast<size_t>(mode)];
}

inline std::optional<CliMode> findMode(std::string_view s)
{
    for (size_t i = 0; i < CliModeStrings.size(); i++)
    {
        if (CliModeStrings[i] == s)
            return static_cast<CliMode>(i);
    }
    return std::nullopt;
}

struct ModeConfig
{
    CliMode currentMode;            ///< Indicates the current operational mode.
    Cli::ContextBase* modeConfig = nullptr;
    nlohmann::ordered_json* configNode = nullptr;       ///< Pointer to the current configuration node.
    std::vector<nlohmann::ordered_json*> modeHistory;   ///< History of configuration nodes for mode management
    nlohmann::ordered_json* modeSchema = nullptr;       ///< Pointer to the current mode's schema
    nlohmann::ordered_json* tempModeSchema = nullptr;   ///< Temporary pointer for schema operations.
};

#endif // MODE_HPP
