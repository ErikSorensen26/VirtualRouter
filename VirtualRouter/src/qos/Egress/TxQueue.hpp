// TxQueue.hpp

#ifndef TX_QUEUE_HPP
#define TX_QUEUE_HPP

#include <BaseQueue.h>
#include <FIFOQueue.hpp>

enum class TxQueueType
{
    FIFO
};

namespace TxQueueFactory
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

#endif // TX_QUEUE_HPP
