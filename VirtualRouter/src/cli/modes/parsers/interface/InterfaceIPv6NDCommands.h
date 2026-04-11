/**
 * @file InterfaceIPv6NDCommands.h
 * @brief CLI parser for the `ipv6 nd` sub-tree of Interface Configuration mode.
 *
 * Defines per-interface IPv6 Neighbor Discovery commands reachable via
 * `ipv6 nd <...>` in `CliMode::Interface`, including Router Advertisement
 * tuning, DAD configuration, cache limits, NUD behaviour, destination guard,
 * and autoconfiguration controls.
 */

#ifndef INTERFACE_IPV6_ND_COMMANDS_H
#define INTERFACE_IPV6_ND_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "interface/configs/InterfaceType.hpp" // IWYU pragma: keep
#include "cli/modes/contexts/Context.hpp"
#include "configs/registry/interface/NdpRegistry.h"

#define NDP_PARAMS DEFINE_PARAMS(config::NdpRegistry)

namespace cli
{
bool InterfaceIPv6ND_AdvertisementInterval_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_AutoconfigDefRoute_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_AutoconfigPrefix_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_CacheExpire_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_CacheInterfaceLimit_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_DADAttempts_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_DADTime_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_DestinationGuard_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_ManagedConfigFlag_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_NaGlean_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_NsInterval_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_NudIGP_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_NudRetry_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_OtherConfigFlag_Handler(NDP_PARAMS);
// bool InterfaceIPv6ND_Prefix_Handler(NDP_PARAMS) {} //TODO
bool InterfaceIPv6ND_RaHopLimitUnspecified_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_RaInterval_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_RaLifetime_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_RaMtuSuppression_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_RaSuppression_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_RaSuppressionAll_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_ReachableTime_Handler(NDP_PARAMS);
bool InterfaceIPv6ND_RouterPreference_Handler(NDP_PARAMS);

#define INTERFACE_IPV6_ND_LIST(X, Y) \
    X(Y, (_COM_, AdvertisementInterval, "advertisement-interval"_tok)) \
    X(Y, (_COM_, AutoconfigDefRoute, "autoconfig"_tok, "default-route"_tok)) \
    X(Y, (_COM_, AutoconfigPrefix, "autoconfig"_tok, "prefix"_tok)) \
    X(Y, (_COM_, CacheExpire, "cache"_tok, "expire"_tok)) \
    X(Y, (_COM_, CacheInterfaceLimit, "cache"_tok, "interface-limit"_tok)) \
    X(Y, (_COM_, DADAttempts, "dad"_tok, "attempts"_tok)) \
    X(Y, (_COM_, DADTime, "dad"_tok, "time"_tok)) \
    X(Y, (_COM_, DestinationGuard, "destination-guard"_tok)) \
    X(Y, (_COM_, ManagedConfigFlag, "managed-config-flag"_tok)) \
    X(Y, (_COM_, NaGlean, "na"_tok, "glean"_tok)) \
    X(Y, (_COM_, NsInterval, "ns-interval"_tok)) \
    X(Y, (_COM_, NudIGP, "nud"_tok, "igp"_tok)) \
    X(Y, (_COM_, NudRetry, "nud"_tok, "retry"_tok)) \
    X(Y, (_COM_, OtherConfigFlag, "other-config-flag"_tok)) \
    X(Y, (_COM_, RaHopLimitUnspecified, "ra"_tok, "hop-limit"_tok, "unspecified"_tok)) \
    X(Y, (_COM_, RaInterval, "ra"_tok, "interval"_tok)) \
    X(Y, (_COM_, RaLifetime, "ra"_tok, "lifetime"_tok)) \
    X(Y, (_COM_, RaMtuSuppression, "ra"_tok, "mtu"_tok, "suppress"_tok)) \
    X(Y, (_COM_, RaSuppression, "ra"_tok, "suppress"_tok)) \
    X(Y, (_COM_, ReachableTime, "reachable-time"_tok)) \
    X(Y, (_COM_, RouterPreference, "router-preference"_tok)) \

/**
 * @brief Parser for the `ipv6 nd` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::Interface` with `InterfaceContext` and exposes all
 * per-interface Neighbor Discovery tuning commands.
 */
DEFINE_CMD_MODE(InterfaceIPv6ND, CliMode::Interface, config::NdpRegistry, INTERFACE_IPV6_ND_LIST);
}

#undef INTERFACE_IPV6_ND_LIST
#undef NDP_PARAMS

#endif // INTERFACE_IPV6_ND_COMMANDS_H
