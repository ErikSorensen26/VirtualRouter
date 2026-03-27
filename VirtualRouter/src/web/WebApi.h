/**
 * @file WebApi.h
 * @brief TCP-based JSON API server for remote management and statistics access.
 */

/**
 * @defgroup WEB Web & Management API
 * @brief TCP and Unix-socket JSON API servers and session management for remote access.
 */

#ifndef WEB_API_H
#define WEB_API_H

#include <nlohmann/json.hpp>
#include <functional>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace web
{

/**
 * @brief Non-blocking TCP server that exposes a newline-delimited JSON API over a network port.
 * @ingroup WEB
 *
 * Mirrors the design of @ref UnixApi but listens on a TCP port instead of a Unix
 * domain socket, making it suitable for remote management clients. It manages
 * multiple concurrent connections through a single epoll instance, buffering
 * inbound bytes until a complete newline-terminated JSON message is available.
 *
 * Callers drive the I/O loop by calling pollOnce() on their own thread. All
 * Handler, onOpen, and onClose callbacks are invoked synchronously within
 * pollOnce().
 *
 * ## Architectural Role
 * Provides the remote-facing management transport. Local tooling and web
 * consoles on the same host should prefer @ref UnixApi to avoid network exposure.
 *
 * ## Lifecycle & Ownership
 * Call bindAndListen() once after construction. Alternatively, adopt an
 * externally created listening socket with adoptListenSocket(). Call stop()
 * or destroy the object to close all connections and the listening socket.
 *
 * ## Concurrency Model
 * Not thread-safe. All calls to pollOnce(), sendJson(), and closeClient()
 * must be made from the same thread.
 *
 * @see UnixApi
 */
class WebApi
{
public:
    /// Callback invoked when a complete JSON message arrives on @p clientFd.
    using Handler = std::function<void(int clientFd, const nlohmann::json& msg, WebApi& self)>;
    /// Callback invoked when a client connects (@p onOpen) or disconnects (@p onClose).
    using ConnCB = std::function<void(int clientFd, const WebApi& self)>;

    WebApi() = default;

    /**
     * @brief Closes the listening socket and all open client connections.
     * @ingroup WEB
     */
    ~WebApi();

    /**
     * @brief Creates, binds, and begins listening on a TCP socket at @p port.
     *
     * The socket is placed in non-blocking mode with SO_REUSEADDR enabled.
     * An epoll instance is created to monitor all connections.
     *
     * @param port     TCP port number to listen on.
     * @param backlog  Maximum length of the pending connection queue.
     * @return         True on success; false if any system call fails.
     */
    bool bindAndListen(uint16_t port, int backlog = 256);

    /**
     * @brief Takes ownership of an already-bound and listening TCP socket.
     *
     * @param listen_fd  File descriptor of the listening TCP socket.
     * @return           True if the epoll instance was successfully created.
     */
    bool adoptListenSocket(int listen_fd);

    /**
     * @brief Processes pending epoll events, accepting new connections and performing I/O.
     *
     * Blocks for up to @p timeoutMs milliseconds. All Handler, onOpen, and
     * onClose callbacks fire synchronously within this call.
     *
     * @param timeoutMs  Maximum wait time in milliseconds; 0 returns immediately.
     */
    void pollOnce(int timeoutMs);

    /**
     * @brief Serializes @p j to JSON, appends a newline, and queues it for delivery to @p clientFd.
     *
     * Data is written non-blocking. If the socket is not immediately writable
     * the data is buffered and flushed on the next writable epoll event.
     *
     * @param clientFd  File descriptor of the target client.
     * @param j         JSON object to send.
     * @return          True if the data was queued; false if @p clientFd is unknown.
     */
    bool sendJson(int clientFd, const nlohmann::json& j);

    /**
     * @brief Closes the connection to @p clientFd and removes it from the epoll set.
     *
     * Fires the onClose callback before releasing resources.
     *
     * @param clientFd  File descriptor to close.
     */
    void closeClient(int clientFd);

    /**
     * @brief Closes the listening socket and all active client connections.
     */
    void stop();

    void setHandler(Handler h) { handler = std::move(h); }
    void setOnOpen(ConnCB cb)  { onOpen = std::move(cb); }
    void setOnClose(ConnCB cb) { onClose = std::move(cb); }

    int listenFd() const { return lfd; }
    int epollFd()  const { return epfd; }

private:
    /**
     * @brief Per-connection I/O buffers.
     */
    struct Conn
    {
        std::string rbuf; ///< Accumulated inbound bytes awaiting a complete newline-terminated JSON object.
        std::string wbuf; ///< Outbound bytes queued for non-blocking write.
    };

    int lfd  = -1; ///< Listening socket file descriptor.
    int epfd = -1; ///< Epoll instance file descriptor.

    Handler handler; ///< Called with a parsed JSON object for each complete inbound message.
    ConnCB onOpen;   ///< Called when a new client connection is accepted.
    ConnCB onClose;  ///< Called just before a client connection is closed.

    std::unordered_map<int, Conn> conns; ///< Active client connections keyed by file descriptor.

    /**
     * @brief Places @p fd in non-blocking mode.
     * @return False if fcntl fails.
     */
    bool makeNodeblock(int fd);

    /**
     * @brief Adds @p fd to the epoll set with the given event mask.
     * @return False if epoll_ctl fails.
     */
    bool addEpoll(int fd, uint32_t events);

    /**
     * @brief Modifies the event mask for @p fd in the epoll set.
     * @return False if epoll_ctl fails.
     */
    bool modEpoll(int fd, uint32_t events);

    /// Removes @p fd from the epoll set without closing it.
    void delEpoll(int fd);

    /// Accepts all pending connections on the listening socket.
    void handleAccept();

    /**
     * @brief Reads available data from @p cfd and dispatches complete messages.
     *
     * Closes the connection on EOF or read error.
     */
    void handleRead(int cfd);

    /**
     * @brief Flushes the write buffer for @p cfd non-blocking.
     *
     * Removes EPOLLOUT from the watch set once the buffer is fully drained.
     */
    void handleWrite(int cfd);

    /// Attempts a non-blocking write of the full write buffer for @p cfd.
    void drainWriteBuffer(int cfd);

    /**
     * @brief Extracts the next newline-terminated line from @p buf into @p like.
     *
     * @param buf   Inbound buffer; consumed up to and including the newline.
     * @param like  Populated with the extracted line (without the trailing newline).
     * @return      True if a complete line was found and extracted.
     */
    static bool popLine(std::string& buf, std::string& like);
};

} // namespace web

#endif // WEB_API_H
