// EigrpTypes.hpp

#ifndef EIGRP_TYPES_HPP
#define EIGRP_TYPES_HPP

#include <cstdint>

namespace routing::eigrp
{
struct KValue
{
    KValue(uint8_t k1 = 1, uint8_t k2 = 0, uint8_t k3 = 1, uint8_t k4 = 0, uint8_t k5 = 0, uint8_t k6 = 0)
        : k1_Bandwidth(k1), k2_Load(k2), k3_Delay(k3), k4_Reliability(k4), k5_MTU(k5), k6_Power(k6) {}

    uint8_t k1_Bandwidth;
    uint8_t k2_Load;
    uint8_t k3_Delay;
    uint8_t k4_Reliability;
    uint8_t k5_MTU;
    uint8_t k6_Power;
};

struct StubConfig
{
    bool isStub = false;
    bool advertiseConnected = true;
    bool advertiseLeakMap = false;
    bool advertiseStatic = true;
    bool advertiseSummary = true;
    bool advertiseRedistributed = true;
    bool receiveOnly = false;
};
} // namespace routing

#endif // EIGRP_TYPES_HPP

