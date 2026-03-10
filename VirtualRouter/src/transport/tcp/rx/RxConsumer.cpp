// RxConsumer.cpp

#include <cstdint>
#include <span>

#include "RxConsumer.h"
#include "RxBuffer.h"

namespace TCP
{
RxConsumer::RxConsumer(RxBuffer& buf, std::span<uint8_t> data)
    : bufferRx(data), rxView(data), buffer(buf) {}

uint64_t RxConsumer::getId() const noexcept
{
    return buffer.cid;
}

void RxConsumer::commit(size_t bytes)
{
    consumed += bytes;
    rxView = std::span<uint8_t>(bufferRx.data() + consumed, bufferRx.size() - consumed);
}

const std::span<uint8_t>& RxConsumer::get() noexcept
{
    return rxView;
}
    
RxConsumer::~RxConsumer()
{
    buffer.commit(consumed);
}
}
