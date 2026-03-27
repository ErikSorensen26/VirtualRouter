/**
 * @file RxQueueOpts.hpp
 * @brief Configuration options for RX queue setup including fanout, batching, and frame management.
 */

#ifndef RX_QUEUE_OPTS_HPP
#define RX_QUEUE_OPTS_HPP

#include <cstdint>
#include <string>
#include <optional>

namespace qos::ingress
{

/**
 * @brief Configuration options for RX queue setup.
 * @ingroup QOS_INGRESS
 *
 * Specifies socket fanout configuration, frame sizing, batching parameters, and
 * CPU core affinity for an RX queue instance.
 */
struct RxQueueOpts
{
    std::string ifname;                    ///< Interface name (e.g., "eth0").
    uint16_t fanoutGroup = 0;              ///< Socket fanout group ID (0 = no fanout).
    int fanoutMode = 0;                    ///< Fanout distribution mode (see SO_FANOUT).

    uint32_t frameCount = 4096;            ///< Target number of frame slots in ring.
    uint32_t frameCountActual = 4096;      ///< Actual frame count (may differ from target).
    uint32_t snapLen = 1024;               ///< Snapshot length per frame (limit captured data).
    uint32_t retireMs = 1;                 ///< Timeout before retiring partial block (ms).
    std::optional<uint32_t> blockSize;     ///< Optional block size (defaults based on frameCount).
    bool ignoreOutgoing = true;            ///< Ignore outgoing frames (RX-only mode).

    int batchTarget = 64;                  ///< Target batch size for processing frames.
    int cpuId = -1;                        ///< CPU core ID for queue thread (-1 = auto-assign).
};

} // namespace qos::ingress

#endif  // RX_QUEUE_OPTS_HPP
