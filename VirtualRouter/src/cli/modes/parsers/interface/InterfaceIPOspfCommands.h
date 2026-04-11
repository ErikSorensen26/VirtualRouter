/**
 * @file InterfaceIPOspfCommands.h
 * @brief CLI parser for the `ip ospf` sub-tree of Interface Configuration mode.
 *
 * Defines OSPFv2 interface-level commands reachable via `ip ospf <...>` in
 * `CliMode::Interface`.  Extends the shared `InterfaceOspfCommands` base with
 * IPv4-specific additions: process-area association, authentication (plain-text
 * and MD5), Link-Local Signalling, prefix suppression, resync timeout, shutdown,
 * and TTL security.
 */

#ifndef INTERFACE_IP_OSPF_COMMANDS_H
#define INTERFACE_IP_OSPF_COMMANDS_H

#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "InterfaceOspfCommands.h"

#define OSPF_PARAMS DEFINE_PARAMS(config::OspfInterfaceBaseRegistry)

namespace cli
{
bool InterfaceIPOspf_Area_Handler(OSPF_PARAMS);
bool InterfaceIPOspf_Authentication_Handler(OSPF_PARAMS);
bool InterfaceIPOspf_AuthenticationKey_Handler(OSPF_PARAMS);
bool InterfaceIPOspf_LLS_Handler(OSPF_PARAMS);
bool InterfaceIPOspf_MessageDigestKey_Handler(OSPF_PARAMS);
bool InterfaceIPOspf_PrefixSuppression_Handler(OSPF_PARAMS);
bool InterfaceIPOspf_ResyncTimeout_Handler(OSPF_PARAMS);
bool InterfaceIPOspf_Shutdown_Handler(OSPF_PARAMS);
bool InterfaceIPOspf_TtlSecurity_Handler(OSPF_PARAMS);

#define INTERFACE_IP_OSPF_LIST(X, Y) \
    X(Y, (_EXT_, InterfaceOspfCommands)) \
    X(Y, (_COM_, Area, P_ARG, "Area"_tok)) \
    X(Y, (_COM_, Authentication, "authentication"_tok)) \
    X(Y, (_COM_, AuthenticationKey, "authentication-key"_tok)) \
    X(Y, (_COM_, LLS, "lls"_tok)) \
    X(Y, (_COM_, MessageDigestKey, "message-digest-key"_tok)) \
    X(Y, (_COM_, PrefixSuppression, "prefix-suppression"_tok)) \
    X(Y, (_COM_, ResyncTimeout, "resync-timeout"_tok)) \
    X(Y, (_COM_, Shutdown, "shutdown"_tok)) \
    X(Y, (_COM_, TtlSecurity, "ttl-security"_tok))

/**
 * @brief Parser for the `ip ospf` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Extends `InterfaceOspfCommands` (shared OSPFv2/v3 base) with OSPFv2-specific
 * interface commands.  Covers `CliMode::Interface` with `InterfaceContext`.
 */
DEFINE_CMD_MODE(InterfaceIPOspf, CliMode::Interface, config::OspfInterfaceBaseRegistry, INTERFACE_IP_OSPF_LIST);
}

#undef INTERFACE_OSPF_LIST
#undef OSPF_PARAMS

#endif // INTERFACE_IP_OSPF_COMMANDS_H
