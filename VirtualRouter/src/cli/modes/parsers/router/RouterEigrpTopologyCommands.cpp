// RouterEigrpTopologyCommands.cpp

#include "RouterEigrpTopologyCommands.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/core/EigrpConfig.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"

namespace Cli
{
bool RouterEigrpTopology_AutoSummary_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getGlobalConfigMgr().setAutoSummary(!ctx.negate);
    return true;
}

bool RouterEigrpTopology_DefaultMetric_Handler(EIGRP_PARAMS)
{
    auto& field = ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::DEFAULT_METRICS>();
    if (!ctx.negate)
    {
        field.set(std::make_tuple(
            static_cast<uint32_t>(std::stoul(args[0])),
            static_cast<uint32_t>(std::stoul(args[1])),
            static_cast<uint8_t>(std::stoul(args[2])),
            static_cast<uint8_t>(std::stoul(args[3])),
            static_cast<uint16_t>(std::stoul(args[4]))
        ));
    }
    else
    {
        field.unset();
    }
    return true;
}

bool RouterEigrpTopology_Distance_Handler(EIGRP_PARAMS)
{
    auto& cfg = ctx.currentEigrp->getGlobalConfigMgr().getConfigs();
    if (args[0] == "eigrp")
    {
        if (!ctx.negate)
        {
            cfg.get<Config::Eigrp::INTERNAL_ADMIN_DISTANCE>().set(static_cast<uint8_t>(std::stoul(args[1])));
            cfg.get<Config::Eigrp::EXTERNAL_ADMIN_DISTANCE>().set(static_cast<uint8_t>(std::stoul(args[2])));
        }
        else
        {
            cfg.get<Config::Eigrp::INTERNAL_ADMIN_DISTANCE>().set(90);
            cfg.get<Config::Eigrp::EXTERNAL_ADMIN_DISTANCE>().set(170);
        }
    }
    return true;
}

bool RouterEigrpTopology_EigrpEventLogSize_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::MAX_EVENT_LOG_SIZE>().set(
        ctx.negate ? 500u : static_cast<uint32_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpTopology_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    if (ctx.currentEigrp->getAF() == AddressFamily::IPv4)
        ctx.terminal.exitMode<CliMode::RouterEigrpAddressFamilyV4>(ctx.currentEigrp, ctx.currentEigrpNamed, nullptr);
    else
        ctx.terminal.exitMode<CliMode::RouterEigrpAddressFamilyV6>(ctx.currentEigrp, ctx.currentEigrpNamed, nullptr);
    return true;
}

bool RouterEigrpTopology_MaximumPaths_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::MAX_PATHS>().set(
        ctx.negate ? 4 : static_cast<uint8_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpTopology_MetricMaximumHops_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::MAX_HOPS>().set(
        ctx.negate ? 100 : static_cast<uint8_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpTopology_ActiveTime_Handler(EIGRP_PARAMS)
{
    auto& cfg = ctx.currentEigrp->getGlobalConfigMgr().getConfigs();
    if (!ctx.negate)
    {
        if (args[0] == "disabled")
        {
            cfg.get<Config::Eigrp::ACTIVE_DISABLED>().set(true);
        }
        else
        {
            cfg.get<Config::Eigrp::ACTIVE_TIME>().set(static_cast<uint16_t>(std::stoul(args[0])));
            cfg.get<Config::Eigrp::ACTIVE_DISABLED>().set(false);
        }
    }
    else
    {
        cfg.get<Config::Eigrp::ACTIVE_TIME>().unset();
        cfg.get<Config::Eigrp::ACTIVE_DISABLED>().set(false);
    }
    return true;
}

bool RouterEigrpTopology_TrafficShare_Handler(EIGRP_PARAMS)
{
    auto& cfg = ctx.currentEigrp->getGlobalConfigMgr().getConfigs();
    if (!ctx.negate)
    {
        if (args[0] == "balanced")
            cfg.get<Config::Eigrp::TRAFFIC_SHARE>().set(EIGRP::TrafficShareMode::BALENCED);
        else if (args[0] == "min")
        {
            if (args.size() == 2 && args[1] == "across-interfaces")
                cfg.get<Config::Eigrp::TRAFFIC_SHARE>().set(EIGRP::TrafficShareMode::MINIMUM_ACROSS_INTERFACE);
            else
                cfg.get<Config::Eigrp::TRAFFIC_SHARE>().set(EIGRP::TrafficShareMode::MINIMUM);
        }
    }
    else
    {
        cfg.get<Config::Eigrp::TRAFFIC_SHARE>().set(EIGRP::TrafficShareMode::BALENCED);
    }
    return true;
}

bool RouterEigrpTopology_Variance_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::VARIANCE>().set(
        ctx.negate ? 1 : static_cast<uint8_t>(std::stoul(args[0])));
    return true;
}
}
