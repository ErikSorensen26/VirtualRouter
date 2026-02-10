// RouterOspfv2Commands.cpp

#include "RouterOspfv2Commands.h"
#include <OspfProcess.h>
#include <CliSession.h>
#include <Functions.h>

namespace Cli
{
bool RouterOspfv2_Area_Handler(OSPF_PARAMS);
bool RouterEigrpTopology_AutoSummary_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getAggregator().enableAutoSummary(!ctx.negate);
    return true;
}

bool RouterEigrpTopology_DefaultMetric_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
