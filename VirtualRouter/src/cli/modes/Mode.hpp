// Mode.hpp

#ifndef MODE_HPP
#define MODE_HPP

#include <string_view>
#include <array>
#include <json.hpp>

#define CLI_MODE_TABLE \
    X(None,                             "") \
    X(UserExec,                         ">") \
    X(PrivilegedExec,                   "#") \
    X(GlobalConfiguration,              "(config)#") \
    X(DhcpGlobalOptions,                "(config-dhcp-global-options)#") \
    X(DhcpConfig,                       "(config-dhcp)#") \
    X(Dhcpv6Config,                     "(config-dhcpv6)#") \
    X(FlowExporter,                     "(config-flow-exporter)#") \
    X(FlowMonitor,                      "(config-flow-monitor)#") \
    X(FlowRecord,                       "(config-flow-record)#") \
    X(Interface,                        "(config-if)#", "ethernet") \
    X(ClassMap,                         "(config-cmap)#") \
    X(Dhcp,                             "(config-dhcp)#") \
    X(ExtendedACL,                      "(config-ext-nacl)#") \
    X(StandardACL,                      "(config-std-nacl)#") \
    X(PolicyMap,                        "(config-pmap)#") \
/*Eigrp*/ \
    X(RouterEigrpNamed,                 "(config-router)#", "eigrp_named") \
    X(RouterEigrpClassicV4,             "(config-router)#", "eigrp_classic") \
    X(RouterEigrpClassicV6,             "(config-rtr)#", "eigrp_classic") \
    X(RouterEigrpClassicVRF,            "(config-router-af)#", "eigrp", "eigrp_classic_vrf") \
    X(RouterEigrpAddressFamilyV4,       "(config-router-af)#", "eigrp", "ipv4") \
    X(RouterEigrpAddressFamilyV6,       "(config-router-af)#", "eigrp", "ipv6") \
    X(RouterEigrpInterfaceV4,           "(config-router-af-interface)#", "ipv4") \
    X(RouterEigrpInterfaceV6,           "(config-router-af-interface)#", "ipv6") \
    X(RouterEigrpTopologyV4,            "(config-router-af-topology)#", "eigrp", "ipv4") \
    X(RouterEigrpTopologyV6,            "(config-router-af-topology)#", "eigrp", "ipv6") \
/*Ospf*/ \

/**
 * @struct Mode
 * @brief Represents various operational modes of the terminal with corresponding command-line prompts.
 */
enum class CliMode
{
#define X(name, ...) name,
    CLI_MODE_TABLE
#undef X
    Count
};

namespace Cli
{
template <typename... Ts>
constexpr auto makePath(Ts&&... xs)
{
    static_assert(sizeof...(Ts) >= 1, "CLI mode path must have at least 1 element");
    static_assert((std::is_convertible_v<Ts, const char*> && ...),
        "CLI mode path elements must be a string literals");

    return std::array<std::string_view, sizeof...(Ts)>{ std::string_view{xs}... };
}

#define X(name, ...) \
    static constexpr auto Path_##name = makePath(__VA_ARGS__);
    CLI_MODE_TABLE
#undef X

using ModePath = std::span<const std::string_view>;

static constexpr std::array<ModePath, static_cast<size_t>(CliMode::Count)> CliModePaths =
{
#define X(name, ...) ModePath{ Path_##name.data(), Path_##name.size() },
    CLI_MODE_TABLE
#undef X
};
}

constexpr std::string_view getPrompt(CliMode mode)
{
    return Cli::CliModePaths[static_cast<size_t>(mode)][0];
}

constexpr Cli::ModePath getPath(CliMode mode)
{
    return Cli::CliModePaths[static_cast<size_t>(mode)];
}

#endif // MODE_HPP
