// RouterEigrpTopologyCommands.cpp

#include "RouterEigrpTopologyCommands.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "cli/parser/CommandUtils.hpp"
#include "cli/runtime/CliSession.h"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpRegistry)

namespace cli
{
bool RouterEigrpTopology_AutoSummary_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    auto& autosum = ctx.configs.get<config::Eigrp::AUTO_SUMMARIZATION>();
    utils::setToggleValue(autosum, ctx);
    return true;
}

bool RouterEigrpTopology_DefaultMetric_Handler(EIGRP_PARAMS)
{
    auto& metrics = ctx.configs.get<config::Eigrp::DEFAULT_METRICS>();
    if (utils::handleValueReset(metrics, ctx))
        return true;

    config::DefType<decltype(metrics)>::type tup;

    if (!utils::setTupleElement(std::get<0>(tup), segs[0] >> 1)) return false;
    if (!utils::setTupleElement(std::get<1>(tup), segs[0] >> 2)) return false;
    if (!utils::setTupleElement(std::get<2>(tup), segs[0] >> 3)) return false;
    if (!utils::setTupleElement(std::get<3>(tup), segs[0] >> 4)) return false;
    if (!utils::setTupleElement(std::get<4>(tup), segs[0] >> 5)) return false;
    metrics.set(tup);
    return true;
}

bool RouterEigrpTopology_Distance_Handler(EIGRP_PARAMS)
{
    auto& internal = ctx.configs.get<config::Eigrp::INTERNAL_ADMIN_DISTANCE>();
    auto& external = ctx.configs.get<config::Eigrp::EXTERNAL_ADMIN_DISTANCE>();
    if (!utils::setFieldValue(internal, ctx, segs >> 0 >> 1))
        return false;
    if (!utils::setFieldValue(external, ctx, segs >> 0 >> 2))
        return false;
    return true;
}

bool RouterEigrpTopology_EigrpEventLogSize_Handler(EIGRP_PARAMS)
{
    auto& logSiz = ctx.configs.get<config::Eigrp::MAX_EVENT_LOG_SIZE>();
    return utils::setFieldValue(logSiz, ctx, segs[0] >> 1);
}

bool RouterEigrpTopologyV4_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.popMode();
}

bool RouterEigrpTopologyV6_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.popMode();
}

bool RouterEigrpTopology_MaximumPaths_Handler(EIGRP_PARAMS)
{
    auto& maxPaths = ctx.configs.get<config::Eigrp::MAX_PATHS>();
    return utils::setFieldValue(maxPaths, ctx, segs[0] >> 1);
}

bool RouterEigrpTopology_MetricMaximumHops_Handler(EIGRP_PARAMS)
{
    auto& metricMax = ctx.configs.get<config::Eigrp::MAXIMUM_PREFIX>();
    return utils::setFieldValue(metricMax, ctx, segs[0] >> 1);
}

bool RouterEigrpTopology_ActiveTime_Handler(EIGRP_PARAMS)
{
    auto& active = ctx.configs.get<config::Eigrp::ACTIVE_TIME>();
    auto& disabled = ctx.configs.get<config::Eigrp::ACTIVE_DISABLED>();

    switch (segs[0][0])
    {
        case "active-time"_tok:
        {
            return utils::setFieldValue(active, ctx, segs[0] >> 1);
        }
        case "disable"_tok:
        {
            utils::setToggleValue(disabled, ctx);
            return true;
        }
    }
    return false;
}

bool RouterEigrpTopology_TrafficShare_Handler(EIGRP_PARAMS)
{
    auto& trafficShare = ctx.configs.get<config::Eigrp::TRAFFIC_SHARE>();
    if (utils::handleValueReset(trafficShare, ctx))
        return true;

    switch (segs[0][0])
    {
        case "balenced"_tok:
        {
            trafficShare.set(config::eigrp::TrafficShareMode::BALENCED);
            return true;
        }
        case "min"_tok:
        {
            trafficShare.set(config::eigrp::TrafficShareMode::MINIMUM);
            return true;
        }
    }

    return false;
}

bool RouterEigrpTopology_Variance_Handler(EIGRP_PARAMS)
{
    auto& variance = ctx.configs.get<config::Eigrp::VARIANCE>();
    return utils::setFieldValue(variance, ctx, segs >> 0 >> 1);
}
}

#undef EIGRP_PARAMS
