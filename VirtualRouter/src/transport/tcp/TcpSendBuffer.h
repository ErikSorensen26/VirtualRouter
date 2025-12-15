// TcpSendBuffer.h

#ifndef TCP_SEND_BUFFER_H
#define TCP_SEND_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace TCP
{
class TcpSendBuffer
{
public:
    explicit TcpSendBuffer(size_t capacityBytes = 256 * 1024);

    size_t capacity() const noexcept;
    size_t size() const noexcept;
    size_t freeSpace() const noexcept;

    uint8_t* reserve(size_t bytes);

    size_t peek(size_t offset, std::span<uint8_t> out) const;
    void consume(size_t bytes);

private:
    std::vector<uint8_t> buf;
    size_t head{0};
    size_t bufSize{0};
    size_t cap{0};
};
} // namespace TCP

#endif // TCP_SEND_BUFFER_H
