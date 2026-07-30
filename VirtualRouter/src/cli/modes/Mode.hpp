/**
 * @file Mode.hpp
 * @brief CLI mode definitions: mode hierarchy, prompts, and navigation paths.
 *
 * Defines all operational modes in the CLI (User Exec, Privileged Exec,
 * Global Configuration, Interface, EIGRP, OSPF, etc.) along with prompt
 * strings and mode navigation paths. Mode hierarchy determines which
 * commands are available at each level.
 */

/**
 * @defgroup CLI_MODES CLI Modes
 * @ingroup CLI
 * @brief Mode definitions, parser contexts, and command trees for each CLI mode.
 */

#ifndef MODE_HPP
#define MODE_HPP

#include <string_view>
#include <array>
#include <span>

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
    X(RouterEigrpAddressFamilyV4,       "(config-router-af)#", "eigrp-ipv4") \
    X(RouterEigrpAddressFamilyV6,       "(config-router-af)#", "eigrp-ipv6") \
    X(RouterEigrpClassicVRF,            "(config-router-af)#", "eigrp-vrf") \
    X(RouterEigrpInterfaceV4,           "(config-router-af-interface)#", "eigrp-ipv4") \
    X(RouterEigrpInterfaceV6,           "(config-router-af-interface)#", "eigrp-ipv6") \
    X(RouterEigrpTopologyV4,            "(config-router-af-topology)#", "eigrp-ipv4") \
    X(RouterEigrpTopologyV6,            "(config-router-af-topology)#", "eigrp-ipv6") \
/*Ospf*/ \

namespace cli
{
/**
 * @enum CliMode
 * @brief CLI operational modes: the current context and available commands.
 *
 * Represents all possible CLI modes the terminal can be in. Each mode
 * has an associated prompt string and navigation path. Modes form a
 * hierarchy: User Exec → Privileged Exec → Global Config → protocol modes.
 *
 * ## Mode Structure
 * - **Exec modes**: UserExec (limited), PrivilegedExec (full access)
 * - **Global Config**: GlobalConfiguration (config-wide settings)
 * - **Protocol modes**: EIGRP (named, classic, AF), OSPF, interface, ACL
 * - **None**: placeholder for inactive/uninitialized mode
 *
 * @see getPrompt(), getPath()
 */
enum class CliMode
{
#define X(name, ...) name,
    CLI_MODE_TABLE
#undef X
    Count
};

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

/**
 * @brief Gets the prompt string for a CLI mode.
 *
 * @param mode The CLI mode to query.
 * @return Prompt string displayed to user (e.g., `#`, `(config)#`).
 */
constexpr std::string_view getPrompt(CliMode mode)
{
    return cli::CliModePaths[static_cast<size_t>(mode)][0];
}

/**
 * @brief Gets the full navigation path for a CLI mode.
 *
 * The path represents the sequence of mode names from root to current.
 * Example: `router`, `eigrp`, `classic` for ClassicV4 mode.
 *
 * @param mode The CLI mode to query.
 * @return Span of mode path components (breadcrumb trail).
 */
constexpr cli::ModePath getPath(CliMode mode)
{
    return cli::CliModePaths[static_cast<size_t>(mode)];
}
}

#endif // MODE_HPP
