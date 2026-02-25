// RxConsumer.h

#ifndef TCP_RX_CONSUMER_H
#define TCP_RX_CONSUMER_H

#include <vector>
#include <cstdint>
#include <span>

namespace TCP
{
class RxBuffer;

class RxConsumer
{
public:

    void commit(size_t bytes);
    const std::span<uint8_t>& get() noexcept;
    ~RxConsumer();
private:
    friend class RxBuffer;

    RxConsumer(RxBuffer& buf, std::span<uint8_t> data);

    std::span<uint8_t> bufferRx;
    std::span<uint8_t> rxView;
    size_t consumed = 0;

    RxBuffer& buffer;
};
}

#endif // TCP_CONSUMER_H

