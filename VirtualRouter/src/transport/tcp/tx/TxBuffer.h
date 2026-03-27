/**
 * @file TxBuffer.h
 * @brief Move-only chained-block transmit buffer for a single TCP connection.
 */

/**
 * @defgroup TCP_TX TCP TX
 * @ingroup TCP
 * @brief TCP transmit buffer and buffer pool.
 */

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

/**
 * @brief Move-only, pool-backed transmit buffer that chains fixed-size blocks for a single TCP connection.
 * @ingroup TCP_TX
 *
 * `TxBuffer` is the write side of the zero-copy TCP send path. It holds a
 * singly-linked chain of @ref TxBufferPool::Block objects and exposes a
 * two-phase write API:
 *
 * 1. @ref reserveSpan — returns a writable span into the tail block (or
 *    allocates a new block from the pool if the tail is full).
 * 2. @ref commit — advances the write cursor by the number of bytes actually
 *    written into the reserved span.
 *
 * Data is read back via @ref peek (returns the head contiguous span) and
 * consumed via @ref consume, which releases fully-drained blocks back to the
 * pool. @ref spliceFrom moves the entire block chain from another `TxBuffer`
 * onto the tail of this one without copying data.
 *
 * ## Architectural Role
 * One `TxBuffer` lives inside each `TcpEngine::ConnectionState`. Protocol
 * code obtains access through a @ref Connection handle. The buffer is
 * intentionally not copyable — ownership is held by the engine, and
 * `Connection` holds only a reference.
 *
 * ## Lifecycle & Ownership
 * Created by @ref TxBufferPool::acquire(); all blocks are returned to the
 * originating pool when the buffer is reset or destroyed. The pool must
 * outlive any `TxBuffer` it created.
 *
 * ## Concurrency Model
 * Not thread-safe. All calls must originate from the thread driving
 * @ref Tcp::pump(). The one exception is `reset()`, which calls
 * @ref TxBufferPool::release() — that method is lock-free and safe to call
 * from another thread after a send has completed.
 *
 * @warning The span returned by `reserveSpan` is invalidated by any subsequent
 * call to `reserveSpan`, `consume`, or `reset`. Write and commit before
 * calling any other method.
 *
 * @see TxBufferPool
 * @see Connection
 */
class TxBuffer final
{
public:
    TxBuffer() noexcept = default;

    /**
     * @brief Destroys the buffer, returning all held blocks to the originating pool.
     */
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

    size_t size()  const noexcept { return sizeBytes; }
    bool   empty() const noexcept { return sizeBytes == 0; }

    /**
     * @brief Reserves a contiguous writable region of at least @p minBytes in the tail block.
     *
     * If the current tail block has insufficient remaining capacity, a new block
     * is acquired from the pool and appended to the chain. The returned span
     * points directly into pool memory — no copy is needed.
     *
     * Returns an empty span if the pool has no free blocks.
     *
     * @param minBytes Minimum number of contiguous bytes required.
     * @return Writable span of at least @p minBytes, or empty on allocation failure.
     *
     * @warning The returned span is invalidated by any subsequent call to
     * `reserveSpan`, `consume`, or `reset`. Write into it and call `commit`
     * before doing anything else with this buffer.
     */
    std::span<uint8_t> reserveSpan(size_t minBytes) noexcept;

    /**
     * @brief Commits @p n bytes written into the most recently reserved span.
     *
     * Advances the tail block's write cursor by @p n and increments `sizeBytes`.
     * Must be called with a value no greater than the length of the span
     * returned by the most recent `reserveSpan` call.
     *
     * @param n Number of bytes to commit.
     */
    void commit(size_t n) noexcept;

    /**
     * @brief Returns the head contiguous readable span starting at @p offset bytes.
     *
     * The span covers bytes `[offset, offset + headBlockReadableBytes)`. It
     * does not cross a block boundary — callers must call `peek` again after
     * consuming data to obtain the next contiguous region.
     *
     * @param offset Byte offset from the current read position.
     * @return Read-only span into the head block; empty if @p offset >= size().
     */
    std::span<const uint8_t> peek(size_t offset) const noexcept;

    /**
     * @brief Consumes up to @p n bytes from the head of the buffer.
     *
     * Advances the read cursor and releases any blocks that become fully
     * consumed back to the pool.
     *
     * @param n Maximum number of bytes to consume.
     * @return Actual number of bytes consumed (may be less than @p n if the
     *         buffer held fewer bytes).
     */
    size_t consume(size_t n) noexcept;

    /**
     * @brief Moves all blocks from @p other onto the tail of this buffer without copying.
     *
     * After the call @p other is empty. Useful for aggregating fragmented
     * writes from multiple sources into a single drain pass.
     *
     * @param other Source buffer; must not be the same object as `*this`.
     */
    void spliceFrom(TxBuffer* other) noexcept;

    /**
     * @brief Immediately releases all blocks back to the pool, leaving the buffer empty.
     *
     * Safe to call from a thread other than the pump thread, because
     * @ref TxBufferPool::release is lock-free.
     */
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

    /// Allocates a fresh block from the pool without linking it into the chain.
    TxBufferPool::Block* newBlock() noexcept;

    /// Allocates a fresh block and appends it to the tail of the chain.
    void appendNewBlock() noexcept;

    /// Ensures the tail block exists and has write capacity, appending a new block if needed.
    void ensureTail() noexcept;

    TxBufferPool* pool = nullptr;

    TxBufferPool::Block* head = nullptr; ///< First block in the chain; next read comes from here.
    TxBufferPool::Block* tail = nullptr; ///< Last block in the chain; next write goes here.

    size_t sizeBytes = 0;     ///< Total readable bytes remaining across all blocks in the chain.
    size_t capacityBytes = 0; ///< Total committed capacity (blockCount * blockSize).
};

} // namespace transport::tcp

#endif // TCP_TX_BUFFER_H
