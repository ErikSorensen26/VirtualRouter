// GlobalIPv6NDCommands.cpp

#include <Global.h>

#include "GlobalIPv6NDCommands.h"
#include "configs/registry/interface/NdpRegistry.h"
#include "cli/parser/CommandUtils.hpp"

#define GLOBAL_PARAMS DEFINE_PARAMS(config::GlobalRegistry)

namespace
{
config::NdpBaseRegistry& getNdpConfigs(cli::Context<config::GlobalRegistry>& ctx)
{
    return ctx.configs.get<config::Global::IPV6_ND>().get();
}
}

namespace cli
{
bool GlobalIPv6ND_CacheExpire_Handler(GLOBAL_PARAMS)
{
    auto& nd = getNdpConfigs(ctx);

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

bool GlobalIPv6ND_CacheIntLimit_Handler(GLOBAL_PARAMS)
{
    auto& nd = getNdpConfigs(ctx);
    
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

bool GlobalIPv6ND_DADTime_Handler(GLOBAL_PARAMS)
{
    auto& nd = getNdpConfigs(ctx);
    auto seg = segs[0];
    return utils::setFieldValue(nd.get<config::NdpBase::DAD_TIME>(), ctx, seg >> 1);
}

bool GlobalIPv6ND_HostMode_Handler(GLOBAL_PARAMS)
{
    UNUSED(segs);
    auto& nd = getNdpConfigs(ctx);
    utils::setToggleValue(nd.get<config::NdpBase::HOST_MODE_STRICT>(), ctx);
    return true;
}

bool GlobalIPv6ND_NSF_Handler(GLOBAL_PARAMS)
{
    auto& nd = getNdpConfigs(ctx);
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

bool GlobalIPv6ND_NudLimit_Handler(GLOBAL_PARAMS)
{
    auto& nd = getNdpConfigs(ctx);
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

bool GlobalIPv6ND_ReachableTime_Handler(GLOBAL_PARAMS)
{
    auto& nd = getNdpConfigs(ctx);
    auto& seg = segs[0];
    return utils::setFieldValue(nd.get<config::NdpBase::REACHABLE_TIME>(), ctx, seg >> 1);
}

bool GlobalIPv6ND_ResolutionLimit_Handler(GLOBAL_PARAMS)
{
    auto& nd = getNdpConfigs(ctx);
    auto& seg = segs[0];
    return utils::setFieldValue(nd.get<config::NdpBase::RESOLUTION_DATA_LIMIT>(), ctx, seg >> 1);
}

bool GlobalIPv6ND_RouteOwner_Handler(GLOBAL_PARAMS)
{
    UNUSED(segs);
    auto& nd = getNdpConfigs(ctx);
    utils::setToggleValue(nd.get<config::NdpBase::ROUTE_OWNER>(), ctx);
    return true;
}
}

#undef GLOBAL_PARAMS
