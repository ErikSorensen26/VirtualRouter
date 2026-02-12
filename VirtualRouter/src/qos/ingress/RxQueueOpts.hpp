// RxQueueOpts.hpp

#ifndef RX_QUEUE_OPTS_HPP
#define RX_QUEUE_OPTS_HPP

#include <cstdint>
#include <string>
#include <optional>

struct RxQueueOpts
{
    std::string ifname;
    uint16_t fanoutGroup = 0;
    int fanoutMode = 0;

    uint32_t frameCount = 4096;
    uint32_t frameCountActual = 4096;
    uint32_t snapLen = 1024;
    uint32_t retireMs = 1;
    std::optional<uint32_t> blockSize;
    bool ignoreOutgoing = true;

    int batchTarget = 64;
    int cpuId = -1;
};

#endif  // RX_QUEUE_OPTS_HPP
