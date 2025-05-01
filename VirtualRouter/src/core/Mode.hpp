#include <string>

#ifndef MODE
#define MODE

/**
 * @struct Mode
 * @brief Represents various operational modes of the terminal with corresponding command-line prompts.
 */
namespace Mode
{
    // Various modes and their corresponding command-line prompts.
    inline std::string userExec = ">";                         ///< User EXEC mode prompt.
    inline std::string privilegedExec = "#";                   ///< Privileged EXEC mode prompt.
    inline std::string globalConfiguration = "(config)#";      ///< Global Configuration mode prompt.

    // DHCP related prompts
    inline std::string dhcpGlobalOptions = "(config-dhcp-global-options)#"; ///< DHCP Global Options Configuration mode prompt.
    inline std::string dhcpConfig = "(config-dhcp)#"; ///< DHCP Pool Configuration mode prompt.
    inline std::string dhcpv6Config = "(config-dhcpv6)#"; ///< DHCPv6 Pool Configuration mode prompt.

    // Netflow related prompts
    inline std::string flowExporter = "(config-flow-exporter)#";   ///< Flow Exporter Configuration mode prompt.
    inline std::string flowMoniter = "(config-flow-moniter)#";     ///< Flow Monitor Configuration mode prompt.
    inline std::string flowRecord = "(config-flow-record)#";       ///< Flow Record Configuration mode prompt.

    // Interface-related prompts.
    inline std::string interface = "(config-if)#";             ///< Interface configuration mode prompt

    // Policy based routing
    inline std::string classMap = "(config-cmap)#";            ///< Class Map mode prompt.
    inline std::string dhcp = "(config-dhcp)#";                ///< DHCP Configuration mode prompt.
    inline std::string extendedACL = "(config-ext-nacl)";      ///< Extended ACL Configuration mode prompt.
    inline std::string standardACL = "(config-std-nacl)#";     ///< Standard ACL Configuration mode prompt.
    inline std::string policyMap = "(config-pmap)#";           ///< Policy Map Configuration mode prompt.

    // Routing protocol prompts
    inline std::string routing = "(config-router)#";            ///< Routing Protocol Configuration mode prompt.
    inline std::string routingV6 = "(config-rtr)#";             ///< Routing Protocol V6 Configuration mode prompt.

    // Address Family prompts
    inline std::string routerAddressFamily = "(config-router-af)#"; ///< Address Family Configuration mode prompt.
    inline std::string routerAddressFamilyInterface = "(config-router-af-interface)#"; ///< Address Family Interface Configuration mode prompt.
    inline std::string routerAddressFamilyTopology = "(config-router-af-topology)#"; ///< Address Family Topology Configuration mode prompt.
};

#endif // MODE
