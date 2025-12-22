// RouterEigrpTopologyCommands.cpp

#include "RouterEigrpTopologyCommands.h"
#include <Eigrp.h>
#include <EigrpTypes.hpp>
#include <CliSession.h>
#include <Functions.h>

namespace Cli
{
bool RouterEigrpTopology_AutoSummary_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getAggregator().enableAutoSummary(!ctx.negate);
    return true;
}

bool RouterEigrpTopology_DefaultMetric_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    if (!ctx.negate)
    {
        std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
        auto& defaultMetric = configs.defaultMetrics;
        defaultMetric.k1_Bandwidth = static_cast<uint8_t>(std::stoi(args[0]));
        defaultMetric.k3_Delay = static_cast<uint8_t>(std::stoi(args[1]));
        defaultMetric.k4_Reliability = static_cast<uint8_t>(std::stoi(args[2]));
        defaultMetric.k2_Load = static_cast<uint8_t>(std::stoi(args[3]));
        defaultMetric.k5_MTU = static_cast<uint8_t>(std::stoi(args[4]));
    }
    else
    {
        std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
        auto& defaultMetric = configs.defaultMetrics;
        defaultMetric.k1_Bandwidth = 1;
        defaultMetric.k2_Load = 0;
        defaultMetric.k3_Delay = 1;
        defaultMetric.k4_Reliability = 0;
        defaultMetric.k5_MTU = 0;
        defaultMetric.k6_Power = 0;
    }
    return true;
}

bool RouterEigrpTopology_Distance_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    if (!ctx.negate)
    {
        configs.adminDistance.store(static_cast<uint8_t>(std::stoi(args[0])), std::memory_order_release);
        configs.externalAdminDistance.store(static_cast<uint8_t>(std::stoi(args[1])), std::memory_order_release);
    }
    else
    {
        configs.adminDistance.store(90, std::memory_order_release);
        configs.externalAdminDistance.store(170, std::memory_order_release);
    }
    return true;
}

bool RouterEigrpTopology_EigrpEventLogSize_Handler(EIGRP_PARAMS)
{
    ctx.negate
        ? ctx.currentEigrp->getConfigs().eventLogSize.store(500, std::memory_order_release)
        : ctx.currentEigrp->getConfigs().eventLogSize.store(static_cast<uint32_t>(std::stoi(args[0])), std::memory_order_release);
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
    auto& configs = ctx.currentEigrp->getConfigs();
    if (!ctx.negate)
    {
        configs.maxPaths.store(static_cast<uint8_t>(std::stoi(args[0])));
    }
    else
    {
        configs.maxPaths.store(static_cast<uint8_t>(4));
    }
    return true;
}

bool RouterEigrpTopology_MetricMaximumHops_Handler(EIGRP_PARAMS)
{
    ctx.negate
        ? ctx.currentEigrp->getConfigs().maxHops.store(100, std::memory_order_release)
        : ctx.currentEigrp->getConfigs().maxHops.store(static_cast<uint8_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool RouterEigrpTopology_ActiveTime_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    if (!ctx.negate)
    {
        if (Functions::isNumber(args[0]))
        {
            configs.stuckInActiveTime.store(static_cast<uint16_t>(std::stoi(args[0]) / 2), std::memory_order_release);
            configs.activeDisabled.store(false, std::memory_order_release);
        }
        else if (args[0] == "disabled")
        {
            configs.activeDisabled.store(true, std::memory_order_release);
        }
    }
    else
    {
        configs.stuckInActiveTime.store(90, std::memory_order_relaxed);
        configs.activeDisabled.store(false, std::memory_order_release);
    }
    return true;
}

bool RouterEigrpTopology_TrafficShare_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    if (!ctx.negate)
    {
        if (args[0] == "balanced")
        {
            configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Balanced, std::memory_order_release);
        }
        else if (args[0] == "min")
        {
            if (args.size() == 2 && args[1] == "across-interfaces")
                configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::MinimumAcrossInterfaces, std::memory_order_release);
            else
                configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Minimum, std::memory_order_release);
        }
    }
    else
    {
        configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Balanced, std::memory_order_release);
    }
    return true;
}

bool RouterEigrpTopology_Variance_Handler(EIGRP_PARAMS)
{
    ctx.negate
      ? ctx.currentEigrp->getGlobalConfigMgr().setVariance(1)
      : ctx.currentEigrp->getGlobalConfigMgr().setVariance(static_cast<uint8_t>(std::stoul(args[0])));
    return true;
}
}
