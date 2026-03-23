// InterfaceIPOspfCommands.h

#ifndef INTERFACE_IP_OSPF_COMMANDS_H
#define INTERFACE_IP_OSPF_COMMANDS_H

#include "InterfaceOspfCommands.h"

namespace cli
{
bool InterfaceIPOspf_Area_Handler(INTERFACE_PARAMS);
using InterfaceIPOspf_Area = commandAdder<InterfaceContext,
    InterfaceIPOspf_Area_Handler,
    ARG, "area"_tok, ARG, ARG_REST
>;

bool InterfaceIPOspf_Authentication_Handler(INTERFACE_PARAMS);
using InterfaceIPOspf_Authentication = commandAdder<InterfaceContext,
    InterfaceIPOspf_Authentication_Handler,
    "authentication"_tok, ARG_REST
>;

bool InterfaceIPOspf_AuthenticationKey_Handler(INTERFACE_PARAMS);
using InterfaceIPOspf_AuthenticationKey = commandAdder<InterfaceContext,
    InterfaceIPOspf_AuthenticationKey_Handler,
    "authentication-key"_tok, ARG_REST
>;

bool InterfaceIPOspf_LLS_Handler(INTERFACE_PARAMS);
using InterfaceIPOspf_LLS = commandAdder<InterfaceContext,
    InterfaceIPOspf_LLS_Handler,
    "lls"_tok, ARG_REST
>;

bool InterfaceIPOspf_MessageDigestKey_Handler(INTERFACE_PARAMS);
using InterfaceIPOspf_MessageDigestKey = commandAdder<InterfaceContext,
    InterfaceIPOspf_MessageDigestKey_Handler,
    "message-digest-key"_tok, ARG, ARG_REST
>;

bool InterfaceIPOspf_PrefixSuppression_Handler(INTERFACE_PARAMS);
using InterfaceIPOspf_PrefixSuppression = commandAdder<InterfaceContext,
    InterfaceIPOspf_PrefixSuppression_Handler,
    "prefix-suppression"_tok, ARG_REST
>;

bool InterfaceIPOspf_ResyncTimeout_Handler(INTERFACE_PARAMS);
using InterfaceIPOspf_ResyncTimeout = commandAdder<InterfaceContext,
    InterfaceIPOspf_ResyncTimeout_Handler,
    "resync-timeout"_tok, ARG_REST
>;

bool InterfaceIPOspf_Shutdown_Handler(INTERFACE_PARAMS);
using InterfaceIPOspf_Shutdown = commandAdder<InterfaceContext,
    InterfaceIPOspf_Shutdown_Handler,
    "shutdown"_tok
>;

bool InterfaceIPOspf_TtlSecurity_Handler(INTERFACE_PARAMS);
using InterfaceIPOspf_TtlSecurity = commandAdder<InterfaceContext,
    InterfaceIPOspf_TtlSecurity_Handler,
    "ttl-security"_tok, ARG_REST
>;

using InterfaceIPOspfCommands = CliModeParser<CliMode::Interface, InterfaceContext,
    InterfaceOspfCommands,
    InterfaceIPOspf_Area,
    InterfaceIPOspf_Authentication,
    InterfaceIPOspf_AuthenticationKey,
    InterfaceIPOspf_LLS,
    InterfaceIPOspf_MessageDigestKey,
    InterfaceIPOspf_PrefixSuppression,
    InterfaceIPOspf_ResyncTimeout,
    InterfaceIPOspf_Shutdown,
    InterfaceIPOspf_TtlSecurity
>;
}

#endif // INTERFACE_IP_OSPF_COMMANDS_H
