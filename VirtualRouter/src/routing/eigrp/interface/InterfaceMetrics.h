// InterfaceMetrics.h

#ifndef EIGRP_INTERFACE_METRICS_H
#define EIGRP_INTERFACE_METRICS_H

#include <cstdint>
#include <chrono>

namespace routing::eigrp
{
class Neighbor;
class EigrpInterface;
struct ReceivedRoute;

class InterfaceMetrics
{
public:
    InterfaceMetrics(EigrpInterface& iface);

    void addRouteMetrics(std::vector<ReceivedRoute>& routes);
    uint64_t calculateCompositeMetric(uint8_t load, uint8_t reliability, uint64_t delay, uint64_t bandwidth);
    uint64_t getLocalMetric();

    double calculateRTT(Neighbor& neighbor, std::chrono::steady_clock::time_point& sendTime);

    void updateRTTEstimate(Neighbor& neighbor, std::chrono::steady_clock::time_point& sendTime);

private:
    EigrpInterface& iface;
};
} // namespace routing

#endif // EIGRP_INTERFACE_METRICS_H

