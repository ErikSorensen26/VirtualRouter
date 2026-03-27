/**
 * @file RxConsumer.h
 * @brief Scoped, RAII view into an RxBuffer for zero-copy receive processing.
 */

#ifndef TCP_RX_CONSUMER_H
#define TCP_RX_CONSUMER_H

#include <vector>
#include <cstdint>
#include <span>

namespace transport::tcp
{
class RxBuffer;

/**
 * @brief Scoped view into an @ref RxBuffer that tracks partial consumption across multiple protocol reads.
 * @ingroup TCP_RX
 *
 * `RxConsumer` is handed to application callbacks (via @ref RecvCallbackCtx)
 * each time the TCP engine delivers inbound data. The consumer wraps a
 * contiguous byte span and lets the protocol layer examine and incrementally
 * commit bytes it has finished processing.
 *
 * The two-phase model works as follows:
 * 1. The protocol reads from the span returned by @ref get().
 * 2. After processing N complete bytes it calls @ref commit(N).
 * 3. On destruction the committed byte count is forwarded to @ref RxBuffer,
 *    which advances its internal read position.
 *
 * Bytes that are not committed remain available on the next callback
 * invocation, allowing protocols to handle partial messages without buffering
 * their own input.
 *
 * ## Architectural Role
 * `RxConsumer` is the read-side interface between @ref TcpEngine and protocol
 * logic. It avoids an extra copy by giving the protocol a direct span into the
 * receive buffer rather than a separate allocation.
 *
 * ## Lifecycle & Ownership
 * Created exclusively by @ref RxBuffer::consume(). Destroying the consumer
 * commits any outstanding consumed bytes back to the owning buffer. The
 * consumer must not outlive the @ref RxBuffer that created it.
 *
 * ## Concurrency Model
 * Not thread-safe. Must be used only within the @ref RecvCallback invocation
 * that delivered it.
 *
 * @warning Storing an `RxConsumer` beyond the callback frame produces a
 * dangling reference to the underlying RxBuffer data.
 *
 * @see RxBuffer
 * @see RecvCallbackCtx
 */
class RxConsumer
{
public:
    /**
     * @brief Commits all consumed bytes back to the owning @ref RxBuffer.
     *
     * Called automatically on destruction; explicit destruction is not
     * required but is safe.
     */
    ~RxConsumer();

    /**
     * @brief Records that @p bytes of the current view have been processed.
     *
     * Advances the internal consumed counter. The bytes are not removed from
     * the buffer until the consumer is destroyed.
     *
     * @param bytes Number of bytes consumed from the head of the current view.
     *
     * @note Calling `commit` multiple times is additive. The total committed
     * count must not exceed the size of the span returned by `get()`.
     */
    void commit(size_t bytes);

    /**
     * @brief Returns the current readable span of inbound data.
     *
     * The span begins at the first unconsumed byte and extends to the end of
     * the data delivered in this receive event. The span is stable for the
     * lifetime of this consumer.
     *
     * @return Read-write span covering all bytes delivered in this receive event.
     */
    const std::span<uint8_t>& get() noexcept;

    uint64_t getId() const noexcept;

private:
    friend class RxBuffer;

    /**
     * @brief Constructs an RxConsumer for a specific buffer and data window.
     *
     * Called exclusively by @ref RxBuffer::consume(). @p data is the complete
     * span of bytes available for this receive event; `bufferRx` retains a
     * copy for the destructor to commit against.
     *
     * @param buf  Owning buffer; must outlive this consumer.
     * @param data Span covering the bytes to be presented to the protocol layer.
     */
    RxConsumer(RxBuffer& buf, std::span<uint8_t> data);

    std::span<uint8_t> bufferRx; ///< Full span of bytes handed to this consumer at construction.
    std::span<uint8_t> rxView;   ///< Adjusted view presented to the caller; may be a sub-span of bufferRx.
    size_t consumed = 0;         ///< Running total of bytes committed via commit().

    RxBuffer& buffer; ///< Owning buffer; receives the final consumed count on destruction.
};
} // namespace transport::tcp

#endif // TCP_CONSUMER_H
