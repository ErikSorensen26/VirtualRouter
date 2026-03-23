// TxBuffer.h

#ifndef TCP_TX_BUFFER_H
#define TCP_TX_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <sys/uio.h>

#include "TxBufferPool.h"

namespace transport::tcp
{
class TxBuffer;

class TxBuffer final
{
public:
    TxBuffer() noexcept = default;
    ~TxBuffer() { reset(); }

    TxBuffer(const TxBuffer&) = delete;
    TxBuffer& operator=(const TxBuffer&) = delete;

    TxBuffer(TxBuffer&& o) noexcept { moveFrom(o); }
    TxBuffer& operator=(TxBuffer&& o) noexcept
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

    void spliceFrom(TxBuffer* other) noexcept;

    // Release all blocks immediately (buffer becomes empty).
    void reset() noexcept;

private:
    friend class TxBufferPool;

    explicit TxBuffer(TxBufferPool* p) noexcept : pool(p) {}

    void moveFrom(TxBuffer& o) noexcept
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

    TxBufferPool::Block* newBlock() noexcept;
    void appendNewBlock() noexcept;
    void ensureTail() noexcept;

    TxBufferPool* pool = nullptr;

    TxBufferPool::Block* head = nullptr;
    TxBufferPool::Block* tail = nullptr;

    size_t sizeBytes = 0;      // total readable bytes remaining across blocks
    size_t capacityBytes = 0;  // total committed capacity (blockCount * blockSize)
};

} // namespace transport::tcp

#endif // TCP_TX_BUFFER_H

