// InterfaceIPv6NDCommands.h

#ifndef INTERFACE_IPV6_ND_COMMANDS_H
#define INTERFACE_IPV6_ND_COMMANDS_H

#include <CliModeParser.hpp>
#include <InterfaceContext.hpp>

namespace Cli
{
bool InterfaceIPv6ND_AdvertisementInterval_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_AdvertisementInterval = commandAdder<InterfaceContext,
    InterfaceIPv6ND_AdvertisementInterval_Handler,
    "advertisement-interval"_tok
>;

bool InterfaceIPv6ND_AutoconfigDefRoute_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_AutoconfigDefRoute = commandAdder<InterfaceContext,
    InterfaceIPv6ND_AutoconfigDefRoute_Handler,
    "autoconfig"_tok, "default-route"_tok
>;

bool InterfaceIPv6ND_AutoconfigPrefix_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_AutoconfigPrefix = commandAdder<InterfaceContext,
    InterfaceIPv6ND_AutoconfigPrefix_Handler,
    "autoconfig"_tok, "prefix"_tok
>;

bool InterfaceIPv6ND_CacheExpire_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_CacheExpire = commandAdder<InterfaceContext,
    InterfaceIPv6ND_CacheExpire_Handler,
    "cache"_tok, "expire"_tok, ARG_REST
>;

bool InterfaceIPv6ND_CacheInterfaceLimit_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_CacheInterfaceLimit = commandAdder<InterfaceContext,
    InterfaceIPv6ND_CacheInterfaceLimit_Handler,
    "cache"_tok, "interface-limit"_tok, ARG_REST
>;

bool InterfaceIPv6ND_DADAttempts_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_DADAttempts = commandAdder<InterfaceContext,
    InterfaceIPv6ND_DADAttempts_Handler,
    "dad"_tok, "attempts"_tok, ARG_REST
>;

bool InterfaceIPv6ND_DADTime_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_DADTime = commandAdder<InterfaceContext,
    InterfaceIPv6ND_DADTime_Handler,
    "dad"_tok, "time"_tok, ARG_REST
>;

bool InterfaceIPv6ND_DestinationGuard_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_DestinationGuard = commandAdder<InterfaceContext,
    InterfaceIPv6ND_DestinationGuard_Handler,
    "destination-guard"_tok
>;

bool InterfaceIPv6ND_ManagedConfigFlag_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_ManagedConfigFlag = commandAdder<InterfaceContext,
    InterfaceIPv6ND_ManagedConfigFlag_Handler,
    "managed-config-flag"_tok
>;

bool InterfaceIPv6ND_NaGlean_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_NaGlean = commandAdder<InterfaceContext,
    InterfaceIPv6ND_NaGlean_Handler,
    "na"_tok, "glean"_tok
>;

bool InterfaceIPv6ND_NsInterval_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_NsInterval = commandAdder<InterfaceContext,
    InterfaceIPv6ND_NsInterval_Handler,
    "ns-interval"_tok, ARG_REST
>;

bool InterfaceIPv6ND_NudIGP_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_NudIGP = commandAdder<InterfaceContext,
    InterfaceIPv6ND_NudIGP_Handler,
    "nud"_tok, "igp"_tok
>;

bool InterfaceIPv6ND_NudRetry_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_NudRetry = commandAdder<InterfaceContext,
    InterfaceIPv6ND_NudRetry_Handler,
    "nud"_tok, "retry"_tok, ARG_REST
>;

bool InterfaceIPv6ND_OtherConfigFlag_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_OtherConfigFlag = commandAdder<InterfaceContext,
    InterfaceIPv6ND_OtherConfigFlag_Handler,
    "other-config-flag"_tok
>;

// bool InterfaceIPv6ND_Prefix_Handler(INTERFACE_PARAMS) {} //TODO

bool InterfaceIPv6ND_RaHopLimitUnspecified_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_RaHopLimitUnspecified = commandAdder<InterfaceContext,
    InterfaceIPv6ND_RaHopLimitUnspecified_Handler,
    "ra"_tok, "hop-limit"_tok, "unspecified"_tok
>;

bool InterfaceIPv6ND_RaInterval_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_RaInterval = commandAdder<InterfaceContext,
    InterfaceIPv6ND_RaInterval_Handler,
    "ra"_tok, "interval"_tok, ARG_REST
>;

bool InterfaceIPv6ND_RaLifetime_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_RaLifetime = commandAdder<InterfaceContext,
    InterfaceIPv6ND_RaLifetime_Handler,
    "ra"_tok, "lifetime"_tok, ARG_REST
>;

bool InterfaceIPv6ND_RaMtuSuppression_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_RaMtuSuppression = commandAdder<InterfaceContext,
    InterfaceIPv6ND_RaMtuSuppression_Handler,
    "ra"_tok, "mtu"_tok, "suppress"_tok
>;

bool InterfaceIPv6ND_RaSuppression_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_RaSuppression = commandAdder<InterfaceContext,
    InterfaceIPv6ND_RaSuppression_Handler,
    "ra"_tok, "suppress"_tok
>;

bool InterfaceIPv6ND_RaSuppressionAll_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_RaSuppressionAll = commandAdder<InterfaceContext,
    InterfaceIPv6ND_RaSuppressionAll_Handler,
    "ra"_tok, "suppress"_tok, "all"_tok
>;

bool InterfaceIPv6ND_ReachableTime_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_ReachableTime = commandAdder<InterfaceContext,
    InterfaceIPv6ND_ReachableTime_Handler,
    "reachable-time"_tok, ARG_REST
>;

bool InterfaceIPv6ND_RouterPreference_Handler(INTERFACE_PARAMS);
using InterfaceIPv6ND_RouterPreference = commandAdder<InterfaceContext,
    InterfaceIPv6ND_RouterPreference_Handler,
    "router-preference"_tok, ARG_REST
>;

using InterfaceIPv6NDCommands = CliModeParser<CliMode::Interface, InterfaceContext,
    InterfaceIPv6ND_AdvertisementInterval,
    InterfaceIPv6ND_AutoconfigDefRoute,
    InterfaceIPv6ND_AutoconfigPrefix,
    InterfaceIPv6ND_CacheExpire,
    InterfaceIPv6ND_CacheInterfaceLimit,
    InterfaceIPv6ND_DADAttempts,
    InterfaceIPv6ND_DADTime,
    InterfaceIPv6ND_DestinationGuard,
    InterfaceIPv6ND_ManagedConfigFlag,
    InterfaceIPv6ND_NaGlean,
    InterfaceIPv6ND_NsInterval,
    InterfaceIPv6ND_NudIGP,
    InterfaceIPv6ND_NudRetry,
    InterfaceIPv6ND_OtherConfigFlag,
    /*InterfaceIPv6ND_Prefix,*/
    InterfaceIPv6ND_RaHopLimitUnspecified,
    InterfaceIPv6ND_RaInterval,
    InterfaceIPv6ND_RaLifetime,
    InterfaceIPv6ND_RaMtuSuppression,
    InterfaceIPv6ND_RaSuppression,
    InterfaceIPv6ND_RaSuppressionAll,
    InterfaceIPv6ND_ReachableTime,
    InterfaceIPv6ND_RouterPreference
>;
}

#endif // INTERFACE_IPV6_ND_COMMANDS_H
