/**
 * @file Listener.h
 * @brief Move-only RAII handle for a passive TCP listening socket.
 */

#ifndef TCP_LISTENER_H
#define TCP_LISTENER_H

#include "TcpTypes.hpp"

namespace transport::tcp
{
class Connection;
class TcpEngine;

/**
 * @brief Move-only RAII handle for a passive TCP listening socket managed by @ref TcpEngine.
 * @ingroup TCP
 *
 * A `Listener` is returned by @ref Tcp::listen() and represents an OS socket
 * in the LISTEN state. Destroying a valid `Listener` closes the socket and
 * removes it from the epoll instance, dropping any pending accepts in the
 * backlog.
 *
 * The `Listener` also acts as a write-side proxy for connections that were
 * accepted through it: `send()` and `disconnect()` are routed through the
 * engine's listener-owned connection table rather than through a standalone
 * @ref Connection handle.
 *
 * ## Architectural Role
 * `Listener` is the caller-facing boundary for the server side of a TCP
 * connection. Protocol code (e.g. BGP) holds a `Listener` for as long as it
 * needs to accept new peers; it never interacts with @ref TcpEngine directly.
 *
 * ## Lifecycle & Ownership
 * Created exclusively by @ref TcpEngine (friended). Only one `Listener` per
 * @ref ListenId may exist at a time. Moving a `Listener` transfers ownership;
 * the moved-from handle is left in the invalid state (`ok()` returns false).
 * The destructor calls `shutdown()` automatically if the handle is still valid.
 *
 * ## Concurrency Model
 * Not thread-safe. All calls must originate from the same thread that drives
 * @ref Tcp::pump().
 *
 * @see Tcp::listen
 * @see Connection
 * @see TcpEngine
 */
class Listener final
{
public:
    /**
     * @brief Constructs a default, invalid Listener.
     *
     * `ok()` returns false until this handle is assigned from a valid
     * `Listener` returned by @ref Tcp::listen().
     */
    Listener() = default;

    /**
     * @brief Destroys the Listener.
     *
     * If `ok()` is true, calls `shutdown()` to close the listening socket and
     * release all resources held by @ref TcpEngine for this listener.
     */
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    /**
     * @brief Move-constructs a Listener, transferring socket ownership.
     *
     * After the move, `other.ok()` returns false.
     *
     * @param other Source handle; left in the invalid state.
     */
    Listener(Listener&& other) noexcept;

    /**
     * @brief Move-assigns a Listener, transferring socket ownership.
     *
     * If `*this` is currently valid, `shutdown()` is called before the
     * assignment. After the move, `other.ok()` returns false.
     *
     * @param other Source handle; left in the invalid state.
     * @return Reference to `*this`.
     */
    Listener& operator=(Listener&& other) noexcept;

    bool ok() const noexcept { return engine && id != 0; }
    explicit operator bool() const noexcept { return ok(); }

    ListenId getId() const noexcept { return id; }

    /**
     * @brief Sends data to an accepted connection owned by this listener.
     *
     * Routes the write through @ref TcpEngine::listenerFlush, which pushes
     * committed TX bytes for the accepted connection identified by @p cid.
     *
     * @param cid  @ref ConnId of the accepted connection to send to.
     * @param data Byte span to transmit; the span must remain valid for the
     *             duration of this call.
     * @return Number of bytes accepted into the TX buffer and flushed.
     *
     * @note The return value may be less than `data.size()` if the send buffer
     * is full. The caller is responsible for retrying on partial sends.
     */
    size_t send(ConnId cid, std::span<const uint8_t>& data) noexcept;

    /**
     * @brief Closes the listening socket and removes it from the event loop.
     *
     * After this call `ok()` returns false. Pending accepted connections
     * are not automatically closed — the caller must disconnect them first.
     */
    void shutdown() noexcept;

    /**
     * @brief Closes an accepted connection owned by this listener.
     *
     * Routes the teardown through @ref TcpEngine::listenerDisconnect. The
     * @ref ConnId @p cid is released and must not be used after this call.
     *
     * @param cid @ref ConnId of the accepted connection to close.
     */
    void disconnect(ConnId cid) noexcept;

private:
    friend class TcpEngine;
    friend class Connection;

    Listener(TcpEngine* e, ListenId lid) noexcept
        : engine(e), id(lid) {}

    TcpEngine* engine{nullptr}; ///< Owning engine; null when the handle is invalid.
    ListenId id{0};             ///< Listener identifier assigned by TcpEngine; 0 when invalid.
};
} // namespace transport::tcp

#endif
