// InterfaceIpv6NDCommands.cpp

#include <VirtualRouter.h>
#include <Global.h>

#include "InterfaceIPv6NDCommands.h"
#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/CommandUtils.hpp"

#define NDP_PARAMS DEFINE_PARAMS(config::NdpRegistry)

namespace cli
{
bool InterfaceIPv6ND_AdvertisementInterval_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& ai = ctx.configs().reg.get<config::Ndp::ADVERTISEMENT_INTERVAL>();
    utils::setToggleValue(ai, ctx);
    return true;
}

bool InterfaceIPv6ND_AutoconfigDefRoute_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& adr = ctx.configs().reg.get<config::Ndp::AUTOCONFIG_DEFAULT_ROUTE>();
    utils::setToggleValue(adr, ctx);
    return true;
}

bool InterfaceIPv6ND_AutoconfigPrefix_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& ap = ctx.configs().reg.get<config::Ndp::AUTOCONFIG_PREFIX>();
    utils::setToggleValue(ap, ctx);
    return true;
}

bool InterfaceIPv6ND_CacheExpire_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs().reg.get<config::Ndp::BASE>().get();

    for (const std::span<Token>& seg : segs)
    {
        switch (seg[0])
        {
            case "expire"_tok:
            {
                if (!utils::setFieldValue(nd.reg.get<config::NdpBase::CACHE_EXPIRE>(), ctx, seg >> 1))
                    return false;
                utils::setToggleValue(nd.reg.get<config::NdpBase::CACHE_REFRESH>(), ctx);
                break;
            }
            case "refresh"_tok:
            {
                return utils::setFieldValue(nd.reg.get<config::NdpBase::CACHE_REFRESH>(), ctx, seg >> 1);
            }
        }
    }
    return true;
}

bool InterfaceIPv6ND_CacheInterfaceLimit_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs().reg.get<config::Ndp::BASE>().get();
    
    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "interface-limit"_tok:
            {
                if (!utils::setFieldValue(nd.reg.get<config::NdpBase::CACHE_INTERFACE_LIMIT>(), ctx, seg >> 1))
                    return false;
                utils::setFieldValue(nd.reg.get<config::NdpBase::CACHE_INTERFACE_LIMIT_LOG_RATE>(), ctx, seg >> 1);
                break;
            }
            case "log"_tok:
            {
                return utils::setFieldValue(nd.reg.get<config::NdpBase::CACHE_INTERFACE_LIMIT_LOG_RATE>(), ctx, seg >> 1);
            }
        }
    }
    return true;
}

bool InterfaceIPv6ND_DADAttempts_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs();
    auto& dadAttempts = nd.reg.get<config::Ndp::DAD_ATTEMPTS>();
    return utils::setFieldValue(dadAttempts, ctx, segs[0] >> 1);
}

bool InterfaceIPv6ND_DADTime_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs().reg.get<config::Ndp::BASE>().get();
    auto seg = segs[0];
    return utils::setFieldValue(nd.reg.get<config::NdpBase::DAD_TIME>(), ctx, seg >> 1);
}

bool InterfaceIPv6ND_DestinationGuard_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& dg = ctx.configs().reg.get<config::Ndp::DESTINATION_GUARD>();
    utils::setToggleValue(dg, ctx);
    return true;
}

bool InterfaceIPv6ND_ManagedConfigFlag_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& mcf = ctx.configs().reg.get<config::Ndp::MANAGED_CONFIG_FLAG>();
    utils::setToggleValue(mcf, ctx);
    return true;
}

bool InterfaceIPv6ND_NaGlean_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& glean = ctx.configs().reg.get<config::Ndp::NA_GLEAN>();
    utils::setToggleValue(glean, ctx);
    return true;
}

bool InterfaceIPv6ND_NsInterval_Handler(NDP_PARAMS)
{
    auto& nsInterval = ctx.configs().reg.get<config::Ndp::NS_INTERVAL>();
    return utils::setFieldValue(nsInterval, ctx, segs[0] >> 1);
}

bool InterfaceIPv6ND_NudIGP_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& nudIgp = ctx.configs().reg.get<config::Ndp::NUD_IGP>();
    utils::setToggleValue(nudIgp, ctx);
    return true;
}

bool InterfaceIPv6ND_NudRetry_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs();

    auto& base = nd.reg.get<config::Ndp::NUD_RETRY>();
    if (!utils::setFieldValue(base, ctx, segs >> 0 >> 1))		
	return false;
    auto& interval = nd.reg.get<config::Ndp::NUD_RETRY_INTERVAL>();
    if (!utils::setFieldValue(interval, ctx, segs >> 0 >> 2))
	return false;
    auto& attempts = nd.reg.get<config::Ndp::NUD_RETRY_ATTEMPTS>();
    if (!utils::setFieldValue(attempts, ctx, segs >> 0 >> 3))
	return false;
    auto& finalWait = nd.reg.get<config::Ndp::NUD_FINAL_WAIT>();
    utils::setFieldValueWithFallback(finalWait, ctx, segs >> 0 >> 4);
    return true;
}

bool InterfaceIPv6ND_OtherConfigFlag_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& ocf = ctx.configs().reg.get<config::Ndp::OTHER_CONFIG_FLAG>();
    utils::setToggleValue(ocf, ctx);
    return true;
}

bool InterfaceIPv6ND_RaHopLimitUnspecified_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& rahop = ctx.configs().reg.get<config::Ndp::RA_HOP_LIMIT_UNSPECIFIED>();
    utils::setToggleValue(rahop, ctx);
    return true;
}

bool InterfaceIPv6ND_RaInterval_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs();
    auto& interval = nd.reg.get<config::Ndp::RA_INTERVAL>();
    auto& minInterval = nd.reg.get<config::Ndp::RA_MIN_INTERVAL>();

    switch (segs[0][0])
    {
	case "interval"_tok:
	{
	    if (utils::handleValueReset(interval, ctx) && utils::handleValueReset(minInterval, ctx))
		return true;
	    uint32_t sec, minSec;
	    if (!utils::setValue(sec, segs[0] >> 1) || !utils::setValue(minSec, segs[0] >> 2))
		return false;
	    interval.set(sec * 1000);
	    minInterval.set(minSec * 1000);
	    return true;
	}
	case "msec"_tok:
	{
	    if (!utils::setFieldValue(interval, ctx, segs[0] >> 1))
		return false;
	    return utils::setFieldValue(minInterval, ctx, segs[0] >> 2);
	}
    }
    return false;
}

bool InterfaceIPv6ND_RaLifetime_Handler(NDP_PARAMS)
{
    auto& raLife = ctx.configs().reg.get<config::Ndp::RA_LIFETIME>();
    return utils::setFieldValue(raLife, ctx, segs[0] >> 1);
}

bool InterfaceIPv6ND_RaMtuSuppression_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& raMtuSupp = ctx.configs().reg.get<config::Ndp::RA_MTU_SUPPRESS>();
    utils::setToggleValue(raMtuSupp, ctx);
    return true;
}

bool InterfaceIPv6ND_RaSuppression_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& raSupp = ctx.configs().reg.get<config::Ndp::RA_SUPPRESS>();
    utils::setToggleValue(raSupp, ctx);
    return true;
}

bool InterfaceIPv6ND_RaSuppressionAll_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& raSupp = ctx.configs().reg.get<config::Ndp::RA_SUPPRESS_ALL>();
    utils::setToggleValue(raSupp, ctx);
    return true;
}

bool InterfaceIPv6ND_ReachableTime_Handler(NDP_PARAMS)
{
    auto& reachableTime = ctx.configs().reg.get<config::Ndp::BASE>().get().reg.get<config::NdpBase::REACHABLE_TIME>();
    return utils::setFieldValue(reachableTime, ctx, segs[0] >> 1);
}

bool InterfaceIPv6ND_RouterPreference_Handler(NDP_PARAMS)
{
    auto& pref = ctx.configs().reg.get<config::Ndp::ROUTER_PREFERENCE>();

    if (utils::handleValueReset(pref, ctx))
	return true;

    switch (segs[0][0])
    {
	case "high"_tok:
	{
	    pref.set(config::ndp::Preference::HIGH);
	    return true;
	}
	case "medium"_tok:
	{
	    pref.set(config::ndp::Preference::MEDIUM);
	    return true;
	}
	case "low"_tok:
	{
	    pref.set(config::ndp::Preference::LOW);
	    return true;
	}
    }
    return false;
}

// bool InterfaceIPv6ND_Prefix_Handler(NDP_PARAMS) {} //TODO

#define INTERFACE_IPV6_ND_LIST(X, Y) \
    X(Y, (COMMAND, AdvertisementInterval, "advertisement-interval"_tok)) \
    X(Y, (COMMAND, AutoconfigDefRoute, "autoconfig"_tok, "default-route"_tok)) \
    X(Y, (COMMAND, AutoconfigPrefix, "autoconfig"_tok, "prefix"_tok)) \
    X(Y, (COMMAND, CacheExpire, "cache"_tok, "expire"_tok)) \
    X(Y, (COMMAND, CacheInterfaceLimit, "cache"_tok, "interface-limit"_tok)) \
    X(Y, (COMMAND, DADAttempts, "dad"_tok, "attempts"_tok)) \
    X(Y, (COMMAND, DADTime, "dad"_tok, "time"_tok)) \
    X(Y, (COMMAND, DestinationGuard, "destination-guard"_tok)) \
    X(Y, (COMMAND, ManagedConfigFlag, "managed-config-flag"_tok)) \
    X(Y, (COMMAND, NaGlean, "na"_tok, "glean"_tok)) \
    X(Y, (COMMAND, NsInterval, "ns-interval"_tok)) \
    X(Y, (COMMAND, NudIGP, "nud"_tok, "igp"_tok)) \
    X(Y, (COMMAND, NudRetry, "nud"_tok, "retry"_tok)) \
    X(Y, (COMMAND, OtherConfigFlag, "other-config-flag"_tok)) \
    X(Y, (COMMAND, RaHopLimitUnspecified, "ra"_tok, "hop-limit"_tok, "unspecified"_tok)) \
    X(Y, (COMMAND, RaInterval, "ra"_tok, "interval"_tok)) \
    X(Y, (COMMAND, RaLifetime, "ra"_tok, "lifetime"_tok)) \
    X(Y, (COMMAND, RaMtuSuppression, "ra"_tok, "mtu"_tok, "suppress"_tok)) \
    X(Y, (COMMAND, RaSuppression, "ra"_tok, "suppress"_tok)) \
    X(Y, (COMMAND, ReachableTime, "reachable-time"_tok)) \
    X(Y, (COMMAND, RouterPreference, "router-preference"_tok)) \

/**
 * @brief Parser for the `ipv6 nd` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::Interface` with `InterfaceContext` and exposes all
 * per-interface Neighbor Discovery tuning commands.
 */
DEFINE_CMD_MODE(InterfaceIPv6ND, config::NdpRegistry, INTERFACE_IPV6_ND_LIST);
}

#undef NDP_PARAMS
