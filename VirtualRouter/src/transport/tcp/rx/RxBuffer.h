/**
 * @file RxBuffer.h
 * @brief Per-connection receive buffer with RAW and BUFFERED operating modes.
 */

/**
 * @defgroup TCP_RX TCP RX
 * @ingroup TCP
 * @brief TCP receive buffer and consumer interface.
 */

#ifndef TCP_RX_BUFFER_H
#define TCP_RX_BUFFER_H

#include <vector>
#include <span>
#include <cstdint>

namespace transport::tcp
{
class RxConsumer;

/**
 * @brief Per-connection inbound data buffer that presents received bytes to the protocol layer via @ref RxConsumer.
 * @ingroup TCP_RX
 *
 * `RxBuffer` sits between the OS `recv()` call in @ref TcpEngine and the
 * application's @ref RecvCallback. Each time the engine receives data it
 * calls @ref consume() with the raw kernel-filled span; the returned
 * @ref RxConsumer is then passed to the protocol callback.
 *
 * The buffer operates in one of two modes, selected automatically based on
 * whether the incoming receive fits in the current kernel buffer:
 *
 * - **RAW** — the consumer's span points directly into the OS receive buffer
 *   with no copy. Used when a single `recv()` delivers a complete, contiguous
 *   message.
 * - **BUFFERED** — bytes are copied into an internal `std::vector` when
 *   partial data must be held across multiple `recv()` calls. The protocol
 *   sees a single contiguous view of the accumulated data.
 *
 * ## Architectural Role
 * One `RxBuffer` lives inside each `TcpEngine::ConnectionState`. The buffer's
 * public `cid` field lets the @ref RxConsumer (and thus the callback) identify
 * which connection the data belongs to without an extra lookup.
 *
 * ## Lifecycle & Ownership
 * Created by @ref TcpEngine when a connection is established or accepted.
 * Destroyed when the connection is closed. The initial capacity @p recvSiz
 * is set based on the listener's `rxSize` or a default for active connections.
 *
 * ## Concurrency Model
 * Not thread-safe. All calls originate from the @ref TcpEngine pump thread.
 *
 * @see RxConsumer
 * @see TcpEngine
 */
class RxBuffer
{
public:
    /**
     * @brief Constructs an RxBuffer associated with the given connection.
     *
     * Reserves @p recvSiz bytes in the internal vector to avoid early
     * reallocations in BUFFERED mode.
     *
     * @param cid      @ref ConnId of the owning connection; stored as the public `cid` field.
     * @param recvSiz  Initial capacity hint for the internal byte vector (BUFFERED mode).
     */
    RxBuffer(uint64_t cid, size_t recvSiz);

    /**
     * @brief Selects how the receive buffer presents data to the protocol layer.
     * @ingroup TCP_RX
     */
    enum class Mode
    {
        RAW,      ///< Consumer span points directly into the kernel receive buffer; zero-copy fast path.
        BUFFERED  ///< Data is copied into the internal vector to assemble partial messages.
    };

    /**
     * @brief Wraps the given kernel-filled span in an @ref RxConsumer for protocol processing.
     *
     * Determines whether to operate in RAW or BUFFERED mode. In BUFFERED mode
     * the bytes from @p data are appended to the internal vector before the
     * consumer is created, so the protocol sees a contiguous view of all
     * accumulated data.
     *
     * @param data Span pointing into the kernel receive buffer; valid only for
     *             the duration of the current @ref TcpEngine pump iteration.
     * @return An @ref RxConsumer scoped to this buffer.
     *
     * @warning Do not store the returned consumer beyond the current pump
     * iteration; in RAW mode its span will point to stale kernel memory.
     */
    RxConsumer consume(std::span<uint8_t> data);

    Mode   getMode() const noexcept { return mode; };
    size_t size()    const noexcept { return buf.size(); }

    const uint64_t cid; ///< @ref ConnId of the owning connection; set at construction and never changed.

private:
    friend RxConsumer;

    /**
     * @brief Advances the buffer's internal state after the protocol has consumed @p consumed bytes.
     *
     * Called by @ref RxConsumer's destructor. In BUFFERED mode this erases the
     * consumed prefix from the internal vector, retaining any unprocessed tail
     * for the next receive event.
     *
     * @param consumed Number of bytes the protocol confirmed it has processed.
     */
    void commit(size_t consumed);

    Mode mode{Mode::RAW};       ///< Current operating mode; switches to BUFFERED when partial data accumulates.
    std::span<uint8_t> raw{};   ///< In RAW mode: span into the kernel buffer; cleared after each consume call.
    std::vector<uint8_t> buf{}; ///< In BUFFERED mode: accumulated bytes awaiting complete-message assembly.
};
} // namespace transport::tcp

#endif // TCP_RX_BUFFER_H
