/**
 * @file Tcp.h
 * @brief Per-VRF TCP stack entry point; owns the TcpEngine and exposes the
 *        Listener/Connection factory API.
 */

#ifndef TCP_H
#define TCP_H

#include <cstdint>
#include <span>

#include "TcpTypes.hpp"
#include "Listener.h"
#include "Connection.h"

namespace core { class VirtualRouter; }

namespace transport { class TcpSegment; }

/**
 * @defgroup TRANSPORT Transport
 * @brief Per-VRF TCP and UDP transport stacks, isolated per VirtualRouter instance.
 */

/**
 * @defgroup TCP TCP
 * @ingroup TRANSPORT
 * @brief Full TCP state machine: connections, listeners, TxBuffer, RxBuffer, retransmit timers.
 */

namespace transport::tcp
{
class TcpEngine;

/**
 * @brief Per-VRF TCP stack facade that owns one @ref TcpEngine instance.
 * @ingroup TCP
 *
 * Each @ref core::VirtualRouter owns exactly one `Tcp` object. All TCP state
 * (listener sockets, connection sockets, TX/RX buffers, epoll fd) lives inside
 * the TcpEngine that `Tcp` allocates on construction. Nothing is shared between
 * different `Tcp` instances, giving full per-VRF isolation.
 *
 * Callers obtain a @ref Listener via `listen()` or an active @ref Connection
 * via `connect()`. Both handles are move-only RAII wrappers; destroying them
 * closes the underlying socket.
 *
 * ## Architectural Role
 * `Tcp` is the boundary between VRF-level protocol logic (e.g. BGP) and the
 * OS TCP sockets managed by TcpEngine. Protocol code holds a `Tcp&` reference
 * and never touches TcpEngine directly.
 *
 * ## Lifecycle & Ownership
 * Created and destroyed by `VirtualRouter`. The destructor tears down the
 * TcpEngine, which closes all open fds and releases all buffer memory.
 * Listener and Connection handles must not be used after the owning `Tcp`
 * object is destroyed.
 *
 * ## Concurrency Model
 * `Tcp` is not thread-safe. All calls (listen, connect, pump, pollEvents) must
 * come from the same thread — typically the VRF event loop. Callbacks registered
 * via @ref ListenOptions / @ref ConnectOptions are invoked synchronously during
 * `pump()` on that same thread.
 *
 * @warning Destroying `Tcp` while another thread is inside `pump()` is
 * undefined behavior. Drain the event loop before destroying the VRF.
 *
 * @see TcpEngine
 * @see Listener
 * @see Connection
 */
class Tcp
{
public:
    /**
     * @brief Constructs a `Tcp` instance bound to the given VRF.
     *
     * Allocates a TcpEngine, creates the epoll fd, and subscribes to
     * interface-down / address-deletion events so that connections whose
     * local address disappears are torn down automatically.
     *
     * @param vrf The VirtualRouter that owns this stack.
     * @param cfg Optional tuning parameters (ephemeral port range, pool size).
     */
    explicit Tcp(core::VirtualRouter& vrf, Config cfg = {});

    /**
     * @brief Destroys the TCP stack.
     *
     * Closes the epoll fd, closes all listener and connection fds, and frees
     * all TxBuffer / RxBuffer memory. Interface-event subscriptions are
     * cancelled before the engine is deleted.
     */
    ~Tcp();

    Tcp(const Tcp&) = delete;
    Tcp& operator=(const Tcp&) = delete;

    Tcp(Tcp&&) = delete;
    Tcp& operator=(Tcp&&) = delete;

    /**
     * @brief Creates a passive TCP listener on the given local endpoint.
     *
     * Binds and listens on `local`. Accepted connections are delivered either
     * via @ref AcceptCallback (push model) or by draining `pollEvents()` for
     * @ref TcpEventType::ACCEPTED events (pull model). Both models may be used
     * simultaneously.
     *
     * @param local  Local address and port to bind. A wildcard address binds
     *               all local addresses on the given port.
     * @param opt    Socket policy, backlog, and optional callbacks.
     * @return A move-only @ref Listener handle. Destroying it closes the socket.
     */
    Listener listen(const TcpEndpoint& local, const ListenOptions& opt = {});

    /**
     * @brief Initiates an active TCP connection to a remote endpoint.
     *
     * The connect is non-blocking. Completion (or failure) is reported via
     * @ref ConnCallback or a @ref TcpEventType::CONNECTED / ERROR event from
     * `pollEvents()`.
     *
     * @param local   Local address and port to bind (use wildcard + port 0 for
     *                OS-assigned ephemeral).
     * @param remote  Remote address and port to connect to.
     * @param opt     Socket policy, interface bind, and optional callbacks.
     * @return A move-only @ref Connection handle.
     */
    Connection connect(const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opt = {});

    /**
     * @brief Closes the listener or connection identified by `id`.
     *
     * The corresponding @ref Listener or @ref Connection handle becomes invalid
     * after this call. Calling `close()` on an already-closed id is a no-op.
     *
     * @param id ConnId or ListenId returned by `connect()` / `listen()`.
     */
    void close(ConnId id);

    /**
     * @brief Collects pending TCP events without driving sends.
     *
     * Fills `outEvents` with up to `outEvents.size()` events and returns the
     * number written. Blocks for at most `timeoutMs` milliseconds if no events
     * are ready.
     *
     * @param[out] outEvents Caller-supplied span to receive events.
     * @param timeoutMs      Maximum wait time in milliseconds (0 = non-blocking).
     * @return Number of events written into `outEvents`.
     */
    size_t pollEvents(std::span<TcpEvent> outEvents, uint32_t timeoutMs = 0);

    /**
     * @brief Drives the full event loop: accepts, receives, connects, and sends.
     *
     * Calls epoll_wait, dispatches callbacks for all ready fds, and flushes
     * pending TX data. This is the preferred entry point for protocol event
     * loops; `pollEvents()` is for callers that prefer a pull model.
     *
     * @param timeoutMs Maximum wait time in milliseconds (0 = non-blocking).
     * @param maxEvents Maximum epoll events to process per call.
     * @return Number of epoll events processed.
     */
    size_t pump(uint32_t timeoutMs = 0, size_t maxEvents = 64);

    /**
     * @brief Injects a raw TCP segment into the engine (reserved for future use).
     *
     * Intended for software-defined packet injection (e.g. from an XDP path)
     * rather than OS-socket-based I/O. Not yet implemented.
     *
     * @param seg The TCP segment to inject.
     */
    void input(const TcpSegment& seg);

private:
    friend class Listener;
    friend class Connection;

    // TCP INTERFACE EVENT SUBSCRIPTIONS
    uint32_t tcpIfDownId;    ///< Event subscription id for interface-down notifications.
    uint32_t tcpIPv4DelId;   ///< Event subscription id for IPv4 address-deletion notifications.
    uint32_t tcpIPv6DelId;   ///< Event subscription id for IPv6 address-deletion notifications.
    uint32_t tcpIPv6LlDelId; ///< Event subscription id for IPv6 link-local address-deletion notifications.

    TcpEngine* engine{nullptr}; ///< Heap-allocated engine; owned exclusively by this Tcp instance.
};

} // namespace transport::tcp

#endif // TCP_H

