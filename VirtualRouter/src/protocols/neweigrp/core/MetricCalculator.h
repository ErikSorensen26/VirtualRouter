// MetricCalculator.h

#ifndef EIGRP_METRIC_CALCULATOR_H
#define EIGRP_METRIC_CALCULATOR_H

#include <cstdint>

namespace Eigrp
{
class Eigrp;
class ReceivedRoute;
class MetricCalculator
{
public:
    MetricCalculator(Eigrp& egrp);

    void setVariance(uint8_t var);
    void refreshInterfaceMetrics();

private:
    Eigrp& base;
};
}

#endif // EIGRP_METRICS_H
