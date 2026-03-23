// TxQueueOpts.hpp

#ifndef TX_QUEUE_OPTS_HPP
#define TX_QUEUE_OPTS_HPP

#include <cstdint>
#include <string>
#include <optional>

namespace qos::egress
{

struct TxQueueOpts
{
    std::string ifname;
    uint16_t fanoutGroup = 0;
    int fanoutMode = 0;

    uint32_t frameCount = 4096;
    uint32_t frameCountActual = 4096;
    uint32_t snapLen = 1024;
    uint32_t retireMs = 1;
    std::optional<uint32_t> blockSize;
    bool ignoreIngoing = true;

    int batchTarget = 64;
    int cpuId = -1;
};

} // namespace qos

#endif  // RX_QUEUE_OPTS_HPP

