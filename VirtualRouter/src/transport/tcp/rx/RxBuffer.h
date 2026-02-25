// RxBuffer

#ifndef TCP_RX_BUFFER_H
#define TCP_RX_BUFFER_H

#include <vector>
#include <span>
#include <cstdint>

namespace TCP
{
class RxConsumer;

class RxBuffer
{
public:
    RxBuffer(size_t recvSiz);

    enum class Mode
    {
        RAW,
        BUFFERED
    };

    RxConsumer consume(std::span<uint8_t> data);
    Mode getMode() const noexcept { return mode; };
    size_t size() const noexcept { return buf.size(); }
    
private:
    friend RxConsumer;
    
    void commit(size_t consumed);

    Mode mode{Mode::RAW};
    std::span<uint8_t> raw{};
    std::vector<uint8_t> buf{};
};
}

#endif // TCP_RX_BUFFER_H
