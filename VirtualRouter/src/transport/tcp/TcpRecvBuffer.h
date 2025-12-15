// TcpRecvBuffer.h

#ifndef TCP_RECV_BUFFER_H
#define TCP_RECV_BUFFER_H

#include <TcpSegment.hpp>

#include <map>

namespace TCP
{
class TcpRecvBuffer
{
public:
    explicit TcpRecvBuffer(size_t capacity = 256 * 1024);

    size_t capacity() const noexcept;
    size_t buffered() const noexcept;
    size_t freeSpace() const noexcept;

    void setRcvNxt(TcpSeq rcvNxt);
    size_t insert(TcpSeq seq, std::span<const uint8_t> payload);
    size_t read(std::span<uint8_t> out);
    void reset();

private:
    size_t cap{0};
    TcpSeq rcvNxt{0};

    std::vector<uint8_t> inOrder;
    size_t inOrderHead{0};

    std::map<TcpSeq, std::vector<uint8_t>> ooo;
};
} // namespace TCP

#endif // TCP_RECV_BUFFER_H
