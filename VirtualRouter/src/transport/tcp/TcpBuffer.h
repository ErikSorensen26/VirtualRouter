// TcpBuffer.h

#ifndef TCP_BUFFER_H
#define TCP_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <sys/uio.h>

#include "TcpBufferPool.h"

namespace TCP
{
class TcpBuffer;

class TcpBuffer final
{
public:
    TcpBuffer() noexcept = default;
    ~TcpBuffer() { reset(); }

    TcpBuffer(const TcpBuffer&) = delete;
    TcpBuffer& operator=(const TcpBuffer&) = delete;

    TcpBuffer(TcpBuffer&& o) noexcept { moveFrom(o); }
    TcpBuffer& operator=(TcpBuffer&& o) noexcept
    {
        if (this != &o)
        {
            reset();
            moveFrom(o);
        }
        return *this;
    }

    size_t size() const noexcept { return sizeBytes; }
    bool empty() const noexcept { return sizeBytes == 0; }

    std::span<uint8_t> reserveSpan(size_t minBytes) noexcept;

    // Commit `n` bytes written into the most recent writableSpan().
    void commit(size_t n) noexcept;

    // head contiguous readable span.
    std::span<const uint8_t> peek(size_t offset) const noexcept;

    // Consume up to n bytes. Releases fully-consumed blocks back to pool.
    size_t consume(size_t n) noexcept;

    void spliceFrom(TcpBuffer* other) noexcept;

    // Release all blocks immediately (buffer becomes empty).
    void reset() noexcept;

private:
    friend class TcpBufferPool;

    explicit TcpBuffer(TcpBufferPool* p) noexcept : pool(p) {}

    void moveFrom(TcpBuffer& o) noexcept
    {
        pool          = o.pool;
        head          = o.head;
        tail          = o.tail;
        sizeBytes     = o.sizeBytes;
        capacityBytes = o.capacityBytes;

        o.pool          = nullptr;
        o.head          = nullptr;
        o.tail          = nullptr;
        o.sizeBytes     = 0;
        o.capacityBytes = 0;
    }

    TcpBufferPool::Block* newBlock() noexcept;
    void appendNewBlock() noexcept;
    void ensureTail() noexcept;

    TcpBufferPool* pool = nullptr;

    TcpBufferPool::Block* head = nullptr;
    TcpBufferPool::Block* tail = nullptr;

    size_t sizeBytes = 0;      // total readable bytes remaining across blocks
    size_t capacityBytes = 0;  // total committed capacity (blockCount * blockSize)
};

} // namespace TCP

#endif // TCP_BUFFER_H
