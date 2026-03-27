/**
 * @file TxQueueOpts.hpp
 * @brief Configuration options for TX queue setup including fanout, batching, and frame management.
 */

#ifndef TX_QUEUE_OPTS_HPP
#define TX_QUEUE_OPTS_HPP

#include <cstdint>
#include <string>
#include <optional>

namespace qos::egress
{

/**
 * @brief Configuration options for TX queue setup.
 * @ingroup QOS_EGRESS
 *
 * Specifies socket fanout configuration, frame sizing, batching parameters, and
 * CPU core affinity for a TX queue instance.
 */
struct TxQueueOpts
{
    std::string ifname;                    ///< Interface name (e.g., "eth0").
    uint16_t fanoutGroup = 0;              ///< Socket fanout group ID (0 = no fanout).
    int fanoutMode = 0;                    ///< Fanout distribution mode (see SO_FANOUT).

    uint32_t frameCount = 4096;            ///< Target number of frame slots in ring.
    uint32_t frameCountActual = 4096;      ///< Actual frame count (may differ from target).
    uint32_t snapLen = 1024;               ///< Snapshot length per frame (unused in TX).
    uint32_t retireMs = 1;                 ///< Timeout before retiring partial block (ms).
    std::optional<uint32_t> blockSize;     ///< Optional block size (defaults based on frameCount).
    bool ignoreIncoming = true;            ///< Ignore frames on input (TX-only mode).

    int batchTarget = 64;                  ///< Target batch size for transmitting frames.
    int cpuId = -1;                        ///< CPU core ID for queue thread (-1 = auto-assign).
};

} // namespace qos::egress

#endif  // TX_QUEUE_OPTS_HPP
