/**
 * @file UnixApi.h
 * @brief Unix domain socket server for local JSON-framed control-plane communication.
 */

#ifndef UNIX_API_H
#define UNIX_API_H

#include <json.hpp>
#include <functional>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace web
{

/**
 * @brief Non-blocking Unix domain socket server that frames messages as newline-delimited JSON.
 * @ingroup WEB
 *
 * Listens on a SOCK_STREAM Unix domain socket and manages multiple concurrent
 * client connections via a single epoll instance. Each connection maintains
 * independent read and write buffers. Inbound data is accumulated until a
 * newline delimiter is found, at which point the complete JSON object is
 * parsed and forwarded to the registered @ref Handler.
 *
 * Callers drive the I/O loop by calling pollOnce() on their own thread.
 * All callbacks (Handler, onOpen, onClose) are invoked synchronously within
 * pollOnce() on the caller's thread.
 *
 * ## Architectural Role
 * Provides the local management transport used by @ref WebSessionManager to
 * accept CLI sessions from web clients running on the same host. The Unix
 * socket avoids network exposure for the control plane; @ref WebApi provides
 * the TCP-facing counterpart for remote access.
 *
 * ## Lifecycle & Ownership
 * Call bindAndListen() once after construction to create and bind the socket.
 * Alternatively, adopt an externally created listening socket with
 * adoptListenSocket(). Destroy or call stop() to close the listening socket
 * and unlink the socket path.
 *
 * ## Concurrency Model
 * Not thread-safe. All calls to pollOnce(), sendJson(), and closeClient()
 * must be made from the same thread. The Unix socket file is unlinked by
 * stop() or the destructor; no other cleanup is needed.
 *
 * @see WebApi
 * @see WebSessionManager
 */
class UnixApi
{
public:
    /// Callback invoked when a complete JSON message arrives on @p fd.
    using Handler = std::function<void(int, const nlohmann::json&, UnixApi&)>;
    /// Callback invoked when a client connects (@p onOpen) or disconnects (@p onClose).
    using ConnCB = std::function<void(int, const UnixApi&)>;

    UnixApi() = default;

    /**
     * @brief Closes the listening socket and all open client connections, then unlinks the socket path.
     * @ingroup WEB
     */
    ~UnixApi();

    /**
     * @brief Creates, binds, and begins listening on a Unix domain socket at @p path.
     *
     * The socket is created with SOCK_STREAM and placed in non-blocking mode.
     * An epoll instance is created to monitor the listening socket and all
     * accepted client connections.
     *
     * @param path     Filesystem path for the Unix socket file.
     * @param backlog  Maximum length of the pending connection queue.
     * @return         True on success; false if any system call fails.
     */
    bool bindAndListen(const std::string& path, int backlog = 256);

    /**
     * @brief Takes ownership of an already-bound and listening socket file descriptor.
     *
     * Useful when the caller wants to control socket creation (e.g. for
     * privilege separation) and hand the fd to UnixApi for I/O management.
     *
     * @param listenFd  File descriptor of the listening Unix socket.
     * @return          True if the epoll instance was successfully created.
     */
    bool adoptListenSocket(int listenFd);

    /**
     * @brief Processes pending epoll events, accepting new connections and performing I/O.
     *
     * Blocks for up to @p timeoutMs milliseconds waiting for activity. All
     * Handler, onOpen, and onClose callbacks are fired synchronously here.
     *
     * @param timeoutMs  Maximum time to wait in milliseconds; 0 returns immediately.
     */
    void pollOnce(int timeoutMs);

    /**
     * @brief Serializes @p j to JSON, appends a newline, and queues it for delivery to @p clientFd.
     *
     * Data is written non-blocking. If the socket is not immediately writable
     * the data is buffered and flushed on the next writable epoll event.
     *
     * @param clientFd  File descriptor of the target client connection.
     * @param j         JSON object to send.
     * @return          True if the data was queued successfully; false if @p clientFd is unknown.
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
     *
     * The socket path is unlinked from the filesystem. After stop(), the
     * object must not be used without calling bindAndListen() again.
     */
    void stop();

    void setHandler(Handler h)  { handler = std::move(h); }
    void setOnOpen(ConnCB cb)   { onOpen = std::move(cb); }
    void setOnClose(ConnCB cb)  { onClose = std::move(cb); }

    int listenFd() const { return lfd; }
    int epollFd()  const { return epfd; }

private:
    /**
     * @brief Per-connection I/O buffers.
     */
    struct Conn
    {
        std::string rbuf; ///< Accumulated inbound bytes waiting for a complete newline-terminated JSON object.
        std::string wbuf; ///< Outbound bytes queued for non-blocking write.
    };

    std::string sockPath; ///< Filesystem path of the bound Unix socket; used by stop() to unlink.

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
    bool makeNonblock(int fd);

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
     * @brief Reads available data from @p cfd into its read buffer and dispatches complete messages.
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
     * @brief Extracts the next newline-terminated line from @p buf into @p line.
     *
     * @param buf   Inbound buffer; consumed up to and including the newline.
     * @param line  Populated with the extracted line (without the trailing newline).
     * @return      True if a complete line was found and extracted.
     */
    static bool popLine(std::string& buf, std::string& line);
};

} // namespace web

#endif // UNIX_API_H
