// TxQueue.hpp

#ifndef TX_QUEUE_HPP
#define TX_QUEUE_HPP

#include "BaseQueue.h"
#include "FIFOQueue.hpp"

namespace qos::egress
{

enum class TxQueueType
{
    FIFO
};

namespace txqueuefactory
{
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

} // namespace qos

#endif // TX_QUEUE_HPP

