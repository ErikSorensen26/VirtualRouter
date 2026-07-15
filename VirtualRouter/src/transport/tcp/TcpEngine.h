/**
 * @file TcpEngine.h
 * @brief Core TCP state machine: socket lifecycle, epoll dispatch, TX/RX buffer management.
 */

#ifndef TCP_ENGINE_H
#define TCP_ENGINE_H

#include <sys/epoll.h>

#include "TcpTypes.hpp"
#include "tcp/tx/TxBuffer.h"
#include "tcp/tx/TxBufferPool.h"
#include "tcp/rx/RxBuffer.h"

namespace core { class VirtualRouter; }

namespace transport::tcp
{
class TcpBuffer;
class Listener;
class Connection;

/**
 * @brief Low-level TCP engine: manages all OS sockets, epoll, and per-connection buffers.
 * @ingroup TCP
 *
 * `TcpEngine` is the implementation core of the TCP stack. It owns:
 * - An epoll fd used to multiplex all listener and connection sockets.
 * - A @ref TxBufferPool shared across all connections for zero-copy TX writes.
 * - Per-listener `ListenerState` and per-connection `ConnectionState` maps.
 *
 * Consumers (BGP, etc.) never interact with `TcpEngine` directly — they go
 * through the @ref Tcp facade, @ref Listener, and @ref Connection handles.
 *
 * ## Architectural Role
 * TcpEngine sits directly on top of Linux sockets (SOCK_STREAM + epoll). It
 * translates OS-level events (EPOLLIN, EPOLLOUT, EPOLLERR) into the
 * `TcpEvent` / callback model exposed by the `Tcp` facade. It does not
 * implement any application protocol logic.
 *
 * ## Lifecycle & Ownership
 * Created and destroyed exclusively by @ref Tcp. The destructor closes the
 * epoll fd and all tracked fds. `TxBufferPool` memory is freed last.
 *
 * ## Concurrency Model
 * `TcpEngine` is single-threaded. All public methods must be called from the
 * same thread that drives `pump()`. The `TxBufferPool` free-list uses
 * lock-free atomics so that @ref TxBuffer::reset() may be called from a
 * different thread after a send completes, but all other TcpEngine state is
 * unguarded.
 *
 * Calls made from inside pump()-invoked callbacks (write, flush, close,
 * connect) are supported: the engine revalidates its lookups after every
 * callback and the connection table never rehashes below maxConnections.
 *
 * @see Tcp
 * @see TxBufferPool
 */
class TcpEngine final
{
public:
    /**
     * @brief Constructs the engine, creates the epoll fd, and seeds the TX buffer pool.
     *
     * @param v The owning VirtualRouter (used for interface-event subscriptions).
     * @param c Engine configuration (pool size, ephemeral port range, etc.).
     */
    explicit TcpEngine(core::VirtualRouter& v, const Config& c = {});

    /**
     * @brief Destroys the engine.
     *
     * Closes all listener and connection fds, then closes the epoll fd.
     * The TxBufferPool destructor frees all slab allocations.
     */
    ~TcpEngine();

    TcpEngine(const TcpEngine&) = delete;
    TcpEngine& operator=(const TcpEngine&) = delete;

    TcpEngine(TcpEngine&&) = delete;
    TcpEngine& operator=(TcpEngine&&) = delete;

    /**
     * @brief Creates a listening socket and registers it with epoll.
     *
     * @param local Local endpoint to bind and listen on.
     * @param opts  Listener configuration including callbacks and socket policy.
     * @return A @ref Listener handle backed by the new socket.
     */
    Listener createListener(const TcpEndpoint& local, const ListenOptions& opts);

    /**
     * @brief Creates an outbound TCP connection socket and begins connect.
     *
     * The connect is non-blocking. Completion is reported via the callback in
     * `opts` or via a subsequent `pollEvents()` / `pump()` call.
     *
     * @param local  Local endpoint to bind (port 0 = OS-assigned ephemeral).
     * @param remote Remote endpoint to connect to.
     * @param opts   Connection configuration including callbacks and socket policy.
     * @return A @ref Connection handle backed by the new socket.
     */
    Connection createConnection(const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opts);

    /**
     * @brief Closes the listener socket and removes it from epoll.
     * @param id The @ref ListenId to close.
     */
    void closeListener(ListenId id) noexcept;

    /**
     * @brief Closes the connection socket and removes it from epoll.
     * @param id The @ref ConnId to close.
     */
    void closeConnection(ConnId id) noexcept;

    /**
     * @brief Closes an accepted connection owned by a listener.
     *
     * @param id  Owning listener id.
     * @param cid Accepted connection id.
     */
    void listenerDisconnect(ListenId id, ConnId cid) noexcept;

    /**
     * @brief Flushes pending TX data for an active (non-listener-owned) connection.
     *
     * @param cid Connection id.
     * @return Number of bytes sent on this call.
     */
    size_t flush(ConnId cid) noexcept;

    /// Pull-mode receive for @p cid into @p out; returns bytes read (0 = nothing available / closed / error).
    size_t read(ConnId cid, std::span<uint8_t> out) noexcept;

    /// Shuts down one or both directions of @p cid; WRITE flushes pending TX before the FIN.
    void shutdownConnection(ConnId cid, TcpShutdown how) noexcept;

    /// Current RFC 793 state of @p cid's socket; CLOSED for unknown ids.
    TcpState connectionState(ConnId cid) const noexcept;

    /**
     * @brief Initiates a graceful shutdown on an active connection.
     *
     * Sends a FIN after all buffered TX data has been flushed. The connection
     * remains in the map until the peer closes or the socket errors.
     *
     * @param cid Connection id.
     */
    void connectionDisconnect(ConnId cid);

    /**
     * @brief Returns the 4-tuple key for the given connection, if it is live.
     *
     * Returns `std::nullopt` if `id` is unknown or the connection has been closed.
     *
     * @param id Connection id.
     */
    std::optional<TcpSocketKey> connectionSocketKey(ConnId id) const noexcept;

    /**
     * @brief Collects pending events without invoking callbacks.
     *
     * @param[out] outEvents Caller-supplied span to receive events.
     * @param timeoutMs      epoll_wait timeout in milliseconds.
     * @return Number of events written into `outEvents`.
     */
    size_t pollEvents(std::span<TcpEvent> outEvents, uint32_t timeoutMs) noexcept;

    /**
     * @brief Full event loop iteration: accepts, receives, connect-completions, and TX flush.
     *
     * Runs `epoll_wait`, dispatches all ready events (invoking registered callbacks),
     * and flushes any newly buffered TX data.
     *
     * @param tcp       The owning @ref Tcp instance passed to callbacks.
     * @param timeoutMs epoll_wait timeout in milliseconds.
     * @param maxEvents Maximum number of epoll events to process per call.
     * @return Number of epoll events processed.
     */
    size_t pump(Tcp& tcp, uint32_t timeoutMs, size_t maxEvents) noexcept;

private:
    /// Internal state for one listening socket.
    struct ListenerState final
    {
        ListenId id{0};          ///< Unique identifier assigned by TcpEngine.
        int fd{-1};              ///< OS file descriptor for the listening socket.
        int af{AF_UNSPEC};       ///< Address family (AF_INET or AF_INET6).

        TcpEndpoint local{};             ///< Bound local address and port.
        size_t backlog{0};               ///< listen() backlog depth.
        TcpSocketPolicy policyApplied{}; ///< Socket-level policy options applied at bind time.
        size_t rxSize = 2048;            ///< Initial RX buffer size allocated for each accepted connection.

        AcceptCallback onAccept{nullptr}; ///< Optional callback invoked for each newly accepted connection.
        void* onAcceptUser{nullptr};      ///< Opaque user pointer passed to @ref onAccept.

        ConnCallback acceptedConnCallback{nullptr}; ///< Per-connection event callback for accepted sockets.
        void* acceptedConnUser{nullptr};            ///< Opaque user pointer passed to @ref acceptedConnCallback.

        RecvCallback recvCallback{nullptr}; ///< Callback invoked when data arrives on an accepted connection.
        void* recvUser{nullptr};            ///< Opaque user pointer passed to @ref recvCallback.

        std::vector<ConnId> accepted; ///< ConnIds of currently-accepted connections owned by this listener.
    };

    /// Internal state for one TCP connection socket (active or accepted).
    struct ConnectionState final
    {
        /**
         * @brief Constructs connection state, acquiring a TX buffer from the pool.
         *
         * @param cid        Unique connection id assigned by TcpEngine.
         * @param pool       Shared TX buffer pool; one block is acquired here.
         * @param recvBufSiz Initial capacity of the RX buffer in bytes.
         */
        ConnectionState(size_t cid, TxBufferPool& pool, size_t recvBufSiz)
            : id(cid), bufferTx(pool.acquire()), bufferRx(cid, recvBufSiz) {}

        const ConnId id; ///< Immutable connection identifier assigned at construction.
        int fd{-1};      ///< OS file descriptor for this socket.

        TcpSocketKey key{};            ///< Cached 4-tuple (local addr/port, remote addr/port).
        ListenId ownerListener{0};     ///< Non-zero if this connection was accepted by a listener.

        bool connectPending{false}; ///< True while a non-blocking connect() has not yet completed.
        bool peerClosed{false};     ///< True after the remote sent FIN (EPOLLIN with 0 bytes).

        TcpError stickyError{}; ///< Latched error; set on EPOLLERR and returned by subsequent API calls.

        ConnCallback cb{nullptr};   ///< Optional callback for connection-level events (connect, error, close).
        void* cbUser{nullptr};      ///< Opaque user pointer passed to @ref cb.

        RecvCallback recvCb{nullptr}; ///< Callback invoked when inbound data is available.
        void* recvUser{nullptr};      ///< Opaque user pointer passed to @ref recvCb.

        TxBuffer bufferTx; ///< Zero-copy TX ring; acquired from TxBufferPool on construction.
        RxBuffer bufferRx; ///< Linear receive buffer; sized by ListenerState::rxSize or ConnectOptions.
    };

    /// High bit of epoll_event::data.u64 set for listener fds; clear for connection fds.
    static constexpr uint64_t kListenerTag = (1ull << 63);
    static constexpr uint64_t kIdMask = ~kListenerTag;

    static uint64_t packListener(ListenId id) noexcept { return kListenerTag | (id & kIdMask); }
    static uint64_t packConn(ConnId id) noexcept { return (id & kIdMask); }
    static bool isListenerTag(uint64_t v) noexcept { return (v & kListenerTag) != 0; }
    static uint64_t unpackId(uint64_t v) noexcept { return (v & kIdMask); }

private:
    /// Allocates the next ephemeral port from the round-robin cursor, wrapping at cfg.ephemeralMax.
    TcpPort allocateEphemeral() noexcept;

    /// Adds @p fd to the epoll instance with the given @p tag and event mask.
    void epAdd(int fd, uint64_t tag, uint32_t events) noexcept;

    /// Removes @p fd from the epoll instance.
    void epDel(int fd) noexcept;

    /// Calls SO_BINDTODEVICE on @p fd if @p b specifies an interface name.
    void bindToDeviceIfRequested(int fd, const TcpInterfaceBind& b);

    /// Binds @p fd to the address and port in @p ep; returns false on failure.
    bool bindEndpoint(int fd, const TcpEndpoint& ep) noexcept;

    /// Reads the current 4-tuple from @p fd via getsockname/getpeername; returns false if the socket is not connected.
    bool getLiveKey(int fd, TcpSocketKey& out) const noexcept;

    /// Returns the current TCP state of @p fd by reading /proc or using getsockopt.
    TcpState linuxState(int fd) const;

    /// Moves the accepted raw socket @p cfd into a new ConnectionState under @p lst; returns its ConnId or 0 on failure (cfd closed).
    ConnId adoptAcceptedSocket(ListenerState& lst, int cfd) noexcept;

    /**
     * @brief Drains the accept queue for listener @p lid, producing events and/or invoking callbacks.
     *
     * The listener is re-looked-up each iteration so callbacks may safely close it.
     *
     * @param lid              The listener whose fd is readable.
     * @param tcp              Owning Tcp instance passed to AcceptCallback.
     * @param acceptEvents     Caller span for pull-mode event output.
     * @param produced         In/out counter of events written to @p acceptEvents.
     * @param invokeCallbacks  If true, AcceptCallback is called per accepted socket.
     * @return Number of sockets accepted this iteration.
     */
    size_t acceptLoop(ListenId lid, Tcp* tcp, std::span<TcpEvent> acceptEvents, size_t& produced, bool invokeCallbacks) noexcept;

    /// Fires ConnCallback (if set) and queues a TcpEvent for a connect-complete or error on @p cid.
    void dispatchConnectEvent(Tcp& tcp, ConnId cid, TcpEventType t, TcpError e) noexcept;

    /// Returns a pointer to the ConnectionState for @p cid, or nullptr if not found.
    ConnectionState* getConnection(ConnId cid) noexcept;

    /// Returns the accepted ConnectionState for (@p lid, @p cid) after verifying ownership, or nullptr.
    ConnectionState* getAcceptedConnectionChecked(ListenId lid, ConnId cid) noexcept;

    /// Closes the fd, removes from epoll, and erases the ConnectionState for @p cid.
    void closeConnectionInternal(ConnId cid) noexcept;

public:
    /**
     * @brief Tears down all connections whose local address matches @p addr.
     *
     * Called by the interface-event subscription in @ref Tcp when an IP address
     * is removed from an interface, ensuring no dangling connections remain for
     * that source address.
     *
     * @param addr The local IP address that has been removed.
     */
    void dropLocalConnections(const types::IPAddress& addr) noexcept;

private:
    friend class Tcp;

    core::VirtualRouter& vr; ///< Owning VRF; used for interface-event subscriptions and address lookups.
    Config cfg;              ///< Engine configuration snapshot (pool size, ephemeral port range, etc.).
    TxBufferPool bufferPool; ///< Shared free-list of fixed-size blocks; all ConnectionState::bufferTx acquire from here.

    int epfd{-1}; ///< epoll file descriptor; valid for the lifetime of the engine.

    ListenId nextListenId{1}; ///< Monotonically increasing listener id allocator.
    ConnId nextConnId{1};     ///< Monotonically increasing connection id allocator.
    TcpPort nextEphemeral{0}; ///< Round-robin cursor within [cfg.ephemeralMin, cfg.ephemeralMax].

    std::unordered_map<ListenId, ListenerState> listeners;   ///< All active listeners keyed by id.
    std::unordered_map<ConnId, ConnectionState> connections; ///< All active connections keyed by id.

    std::vector<epoll_event> epScratch; ///< Reusable scratch buffer for epoll_wait output.
    std::vector<uint8_t> ioScratch;     ///< Reusable scratch buffer for recv() calls.

    ConnId inCallbackCid{0};  ///< Connection whose recv callback is currently running; closes to it are deferred.
    bool deferredClose{false}; ///< Set when a recv callback closed its own connection; applied after the callback returns.
};
} // namespace transport::tcp

#endif // TCP_ENGINE_H

