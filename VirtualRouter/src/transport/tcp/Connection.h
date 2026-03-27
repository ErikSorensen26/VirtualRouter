/**
 * @file Connection.h
 * @brief Move-only RAII handle for a single TCP connection socket.
 */

#ifndef TCP_CONNECTION_H
#define TCP_CONNECTION_H

#include "TcpTypes.hpp"
#include "tx/TxBuffer.h"

namespace transport::tcp
{
class Listener;
class TcpEngine;
class TxBuffer;

/**
 * @brief Move-only RAII handle for a single TCP connection (active or accepted).
 * @ingroup TCP
 *
 * A `Connection` is the caller-facing handle to one TCP socket managed by
 * @ref TcpEngine. It provides a zero-copy write path via `reserveSpan()` /
 * `flush()`, and a copy-friendly path via the `write()` helper added in
 * `Connection.cpp`.
 *
 * Connections are created by:
 * - @ref Tcp::connect() — active outbound connections.
 * - @ref AcceptCallback — server-side connections handed to the caller inside
 *   the accept callback; the @ref Listener retains ownership of the socket,
 *   but the caller may take a `Connection` handle from `AcceptCallbackCtx`.
 *
 * ## Architectural Role
 * `Connection` is the write-side boundary between protocol logic and the TCP
 * engine. It holds a reference to the owning engine's `TxBuffer` so that
 * protocol code can write directly into preallocated pool memory.
 *
 * ## Lifecycle & Ownership
 * Moving a `Connection` transfers ownership. Destroying a non-empty
 * `Connection` calls `disconnect()`, which initiates a graceful shutdown.
 * A moved-from `Connection` is in the invalid state (`ok()` returns false).
 *
 * ## Concurrency Model
 * Not thread-safe. All calls must come from the thread driving `Tcp::pump()`.
 *
 * @warning Using a `Connection` after the owning `Tcp` instance has been
 * destroyed is undefined behavior.
 *
 * @see Tcp::connect
 * @see TxBuffer
 * @see RxConsumer
 */
class Connection final
{
public:
    /**
     * @brief Destroys the handle.
     *
     * If `ok()`, calls `disconnect()` to initiate a graceful shutdown before
     * releasing the handle.
     */
    ~Connection();

    Connection(const Connection& other) = delete;
    Connection& operator=(const Connection& other) = delete;

    /**
     * @brief Move-constructs a `Connection`, transferring socket ownership.
     *
     * After the move `other.ok()` returns false.
     *
     * @param other Source handle; left in the invalid state.
     */
    Connection(Connection&& other) noexcept;

    /**
     * @brief Move-assigns a `Connection`, transferring socket ownership.
     *
     * If `*this` is currently valid, `disconnect()` is called before the
     * assignment. After the move `other.ok()` returns false.
     *
     * @param other Source handle; left in the invalid state.
     * @return Reference to `*this`.
     */
    Connection& operator=(Connection&& other) noexcept;

    bool ok() const noexcept { return engine && id != 0; }
    explicit operator bool() const noexcept { return ok(); }

    ConnId getId() const noexcept { return id; }

    /**
     * @brief Reserves a contiguous writable span of at least `minBytes` in the TX buffer.
     *
     * This is the zero-copy write path. The caller writes directly into the
     * returned span and then calls @ref TxBuffer::commit() on the owning
     * @ref TxBuffer, followed by `flush()` to send.
     *
     * Returns an empty span if the pool has no free blocks.
     *
     * @param minBytes Minimum usable bytes required in the returned span.
     * @return Writable span of at least `minBytes`, or empty on allocation failure.
     *
     * @warning The returned span is invalidated by any subsequent call to
     * `reserveSpan()` or `flush()`. Write and commit before calling either.
     */
    std::span<uint8_t> reserveSpan(size_t minBytes) noexcept;

    /**
     * @brief Transmits all committed bytes in the TX buffer.
     *
     * Calls `send()` in a loop until the buffer is empty or the socket would
     * block. Unconsumed bytes remain in the buffer for the next `flush()`.
     *
     * @return Total bytes sent on this call.
     */
    size_t flush() noexcept;

    /**
     * @brief Initiates an orderly shutdown of this connection.
     *
     * Sends a FIN after any remaining buffered TX data has been flushed.
     * After `disconnect()` the handle is invalid.
     */
    void disconnect() noexcept;

    /**
     * @brief Returns the 4-tuple (local addr/port, remote addr/port) for this socket.
     *
     * Returns `std::nullopt` if the connection has been closed or the engine
     * does not recognize the id.
     */
    std::optional<TcpSocketKey> socketKey() const noexcept;

private:
    friend class RxConsumer;
    friend class TcpEngine;
    friend class Listener;

    Connection(TcpEngine* e, ConnId cid, TxBuffer& bufTx)
        : bufferTx(bufTx), engine(e), id(cid) {}

    TxBuffer& bufferTx; ///< Reference into TcpEngine::ConnectionState::bufferTx — valid while engine is alive.

    TcpEngine* engine{nullptr}; ///< Owning engine; null when the handle is in the moved-from state.
    ConnId id{0};               ///< Unique connection identifier assigned by TcpEngine; 0 when invalid.
};
} // namespace transport::tcp

#endif // TCP_CONNECTION_H

