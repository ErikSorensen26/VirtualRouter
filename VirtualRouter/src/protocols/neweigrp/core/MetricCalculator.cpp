// EigrpMetrics.cpp

#include "MetricCalculator.h"
#include "EigrpCore.h"
#include "EigrpConfig.h"
#include <cstdint>
#include <EigrpTypes.hpp>

namespace Eigrp
{
MetricCalculator::MetricCalculator(Eigrp& base) : base(base) {}


void MetricCalculator::setVariance(uint8_t var)
{
    if ( var == 0 ) return; // Invalid variance
    base.getGlobalConfigMgr().setVariance(var);
    base.getTopology().recalculateAll();
}
}
