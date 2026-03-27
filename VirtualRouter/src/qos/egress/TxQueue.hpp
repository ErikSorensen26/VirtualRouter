/**
 * @file TxQueue.hpp
 * @brief TX queue type enumeration and factory for creating egress queue instances.
 */

// TxQueue.hpp

#ifndef TX_QUEUE_HPP
#define TX_QUEUE_HPP

#include "BaseQueue.h"
#include "FIFOQueue.hpp"

namespace qos::egress
{

/**
 * @enum TxQueueType
 * @brief Available TX queue implementations.
 * @ingroup QOS_EGRESS
 */
enum class TxQueueType
{
    FIFO  ///< FIFO queue (first-in-first-out, no prioritization).
};

/**
 * @namespace txqueuefactory
 * @brief Factory for creating TX queue instances based on type.
 */
namespace txqueuefactory
{
    /**
     * @brief Creates a TX queue instance of the specified type.
     *
     * Allocates and initializes a queue with the given capacity and egress backend.
     * Returns nullptr if type is unsupported.
     *
     * @param type Queue type to create (@ref TxQueueType::FIFO).
     * @param capacity Queue capacity (maximum pending packets).
     * @param eg Egress backend to transmit frames through.
     * @return Pointer to newly allocated BaseQueue on success, nullptr if unsupported type.
     *
     * @warning Caller is responsible for deleting the returned queue.
     */
    inline static BaseQueue* create(TxQueueType type, uint32_t capacity, EgressBase& eg)
    {
        switch (type)
        {
            case TxQueueType::FIFO:
                return new FIFOQueue(capacity, eg);
            default:
                return nullptr;
        }
        return nullptr;
    }
};

} // namespace qos::egress

#endif // TX_QUEUE_HPP

