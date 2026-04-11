// GlobalIPv6NDCommands.cpp

#include "GlobalIPv6NDCommands.h"
#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/CommandUtils.hpp"
#include "configs/registry/interface/NdpRegistry.h"
#include "cli/parser/CommandUtils.hpp"

#define NDP_PARAMS DEFINE_PARAMS(config::NdpBaseRegistry)

namespace cli
{
bool GlobalIPv6ND_CacheExpire_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs;

    for (const std::span<Token>& seg : segs)
    {
        switch (seg[0])
        {
            case "expire"_tok:
            {
                if (!utils::setFieldValue(nd.get<config::NdpBase::CACHE_EXPIRE>(), ctx, seg >> 1))
                    return false;
                utils::setToggleValue(nd.get<config::NdpBase::CACHE_REFRESH>(), ctx);
                break;
            }
            case "refresh"_tok:
            {
                return utils::setFieldValue(nd.get<config::NdpBase::CACHE_REFRESH>(), ctx, seg >> 1);
            }
        }
    }
    return true;
}

bool GlobalIPv6ND_CacheIntLimit_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs;
    
    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "interface-limit"_tok:
            {
                if (!utils::setFieldValue(nd.get<config::NdpBase::CACHE_INTERFACE_LIMIT>(), ctx, seg >> 1))
                    return false;
                utils::setFieldValue(nd.get<config::NdpBase::CACHE_INTERFACE_LIMIT_LOG_RATE>(), ctx, seg >> 1);
                break;
            }
            case "log"_tok:
            {
                return utils::setFieldValue(nd.get<config::NdpBase::CACHE_INTERFACE_LIMIT_LOG_RATE>(), ctx, seg >> 1);
            }
        }
    }
    return true;
}

bool GlobalIPv6ND_DADTime_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs;
    auto seg = segs[0];
    return utils::setFieldValue(nd.get<config::NdpBase::DAD_TIME>(), ctx, seg >> 1);
}

bool GlobalIPv6ND_HostMode_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& nd = ctx.configs;
    utils::setToggleValue(nd.get<config::NdpBase::HOST_MODE_STRICT>(), ctx);
    return true;
}

bool GlobalIPv6ND_NSF_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs;
    for (const auto& seg : segs)
    {
        switch (seg[0]) 
        {
            case "convergence"_tok:
            {
                return utils::setFieldValue(nd.get<config::NdpBase::NSF_CONVERGENCE_TIME>(), ctx, seg >> 1);
            }
            case "supperssion"_tok:
            {
                return utils::setFieldValue(nd.get<config::NdpBase::NSF_DAD_SUPPRESS>(), ctx, seg >> 1);
            }
            case "throttle"_tok:
            {
                return utils::setFieldValue(nd.get<config::NdpBase::NSF_THROTTLE_RESOLUTIONS>(), ctx, seg >> 1);
            }
        }
    }
    return true;
}

bool GlobalIPv6ND_NudLimit_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs;
    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "limit"_tok:
            {
                if (!utils::setFieldValue(nd.get<config::NdpBase::NUD_LIMIT>(), ctx, seg >> 1))
                    return false;
                utils::handleValueReset(nd.get<config::NdpBase::NUD_REFRESH_PERIOD>(), ctx);
                break;
            }
            case "refresh"_tok:
            {
                return utils::setFieldValue(nd.get<config::NdpBase::NUD_REFRESH_PERIOD>(), ctx, seg >> 1);
            }
        }
    }
    return true;
}

bool GlobalIPv6ND_ReachableTime_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs;
    auto& seg = segs[0];
    return utils::setFieldValue(nd.get<config::NdpBase::REACHABLE_TIME>(), ctx, seg >> 1);
}

bool GlobalIPv6ND_ResolutionLimit_Handler(NDP_PARAMS)
{
    auto& nd = ctx.configs;
    auto& seg = segs[0];
    return utils::setFieldValue(nd.get<config::NdpBase::RESOLUTION_DATA_LIMIT>(), ctx, seg >> 1);
}

bool GlobalIPv6ND_RouteOwner_Handler(NDP_PARAMS)
{
    UNUSED(segs);
    auto& nd = ctx.configs;
    utils::setToggleValue(nd.get<config::NdpBase::ROUTE_OWNER>(), ctx);
    return true;
}

#define GLOBAL_IPV6_ND_LIST(X, Y) \
    X(Y, (COMMAND, CacheExpire, "cache"_tok, "expire"_tok)) \
    X(Y, (COMMAND, CacheIntLimit, "cache"_tok, "interface-limit"_tok)) \
    X(Y, (COMMAND, DADTime, "dad"_tok, "time"_tok)) \
    X(Y, (COMMAND, HostMode, "host"_tok, "mode"_tok, "strict"_tok)) \
    X(Y, (COMMAND, NSF, "nsf"_tok)) \
    X(Y, (COMMAND, NudLimit, "nud"_tok, "limit"_tok)) \
    X(Y, (COMMAND, ReachableTime, "reachable-time"_tok)) \
    X(Y, (COMMAND, ResolutionLimit, "resolution"_tok, "data"_tok, "limit"_tok)) \
    X(Y, (COMMAND, RouteOwner, "route-owner"_tok)) \

DEFINE_CMD_MODE(GlobalIPv6ND, config::NdpBaseRegistry, GLOBAL_IPV6_ND_LIST)
}

#undef NDP_PARAMS
