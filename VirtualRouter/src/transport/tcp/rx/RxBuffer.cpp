// RxBuffer.cpp

#include <cstring>

#include "RxBuffer.h"
#include "RxConsumer.h"

namespace TCP
{
RxBuffer::RxBuffer(size_t recvSiz)
    : buf(recvSiz) {}

void RxBuffer::commit(size_t consumed)
{
    if (mode == Mode::BUFFERED)
    {
        buf.erase(buf.begin(), buf.begin() + std::min(buf.size(), consumed));
        if (buf.empty()) mode = Mode::RAW;
    }
    else
    {
        size_t actualConsumed = std::min(consumed, raw.size());
        if (actualConsumed != raw.size())
        {
            mode = Mode::BUFFERED;
            size_t unconsumed = actualConsumed - consumed;
            buf.resize(unconsumed);
            std::memcpy(buf.data(), raw.data() + consumed, unconsumed);
        }
    }
}

RxConsumer RxBuffer::consume(std::span<uint8_t> data)
{
    if (mode == Mode::RAW)
    {
        raw = data;
        return RxConsumer(*this, raw);
    }
    else
    {
        size_t oldSize = buf.size();
        buf.resize(oldSize + data.size());
        std::memcpy(buf.data() + oldSize, data.data(), data.size());
        return RxConsumer(*this, std::span<uint8_t>(buf));
    }
}
}
