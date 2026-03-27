/**
 * @file WebSessionManager.hpp
 * @brief Manages the lifecycle of CLI sessions initiated from web clients over a Unix socket.
 */

#ifndef WEB_SESSION_MANAGER_HPP
#define WEB_SESSION_MANAGER_HPP

#include <Global.h>
#include "UnixApi.h"
#include "WebConsole.hpp"

#include <json.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <unordered_map>
#include <cstdio>
#include <future>

namespace web
{

/**
 * @brief A per-connection record pairing a CLI session with an async completion handle.
 *
 * The promise/future pair is reserved for future use to signal session
 * teardown across thread boundaries. Currently the session pointer is the
 * primary useful field.
 */
struct WebSession
{
    std::promise<bool> promise; ///< Signalled when the session is fully torn down.
    std::future<bool> future;   ///< Consumed by the creator to wait for teardown completion.

    /**
     * @brief Constructs a WebSession wrapping the given CLI session.
     * @param ses  Pointer to the CLI session this web session drives.
     */
    WebSession(cli::CliSession* ses) : future(promise.get_future()), session(ses) {}

    cli::CliSession* session; ///< The underlying CLI session that processes commands.
};

/**
 * @brief Accepts web client connections and routes JSON commands to per-client CLI sessions.
 * @ingroup WEB
 *
 * WebSessionManager owns the @ref UnixApi server and maintains the mapping
 * between client identifiers (string CIDs), file descriptors, and
 * @ref cli::CliSession instances. Each web client that sends a "subscribe"
 * message gets a dedicated CLI session backed by a @ref WebConsole; command
 * output is buffered there and flushed back to the client as JSON replies.
 *
 * The following JSON actions are handled:
 * - **subscribe** — creates or resumes a CLI session for the given client_id.
 * - **unsubscribe** — tears down the session and sends a confirmation.
 * - **cmd** — forwards the "data" field as a CLI input line and flushes output.
 *
 * ## Architectural Role
 * The single integration point between the web transport layer and the CLI
 * engine. It translates the stateless JSON protocol into stateful CLI sessions
 * and marshals output back to the correct connection.
 *
 * ## Lifecycle & Ownership
 * Constructed with a reference to @ref core::Global and the Unix socket path.
 * The constructor sets up the @ref UnixApi callbacks and starts listening.
 * Call loop() to enter the event loop; it runs indefinitely and must be
 * called on a dedicated thread. Destroy the object to close the socket.
 *
 * ## Concurrency Model
 * All operations run on the single thread that calls loop(). CLI sessions are
 * created and destroyed synchronously within JSON message callbacks, which are
 * themselves invoked inside pollOnce(). No locking is required because there
 * is no cross-thread state access.
 *
 * @warning Destroying a WebSessionManager while loop() is running on another
 * thread will corrupt state. Ensure loop() returns before destruction.
 *
 * @see WebConsole
 * @see UnixApi
 */
class WebSessionManager
{
public:
    /**
     * @brief Constructs the manager, registers UnixApi callbacks, and begins listening.
     *
     * Sets up the JSON message and connection-close handlers on the @ref UnixApi
     * instance before calling bindAndListen(), so no messages can arrive before
     * the callbacks are installed.
     *
     * @param g    Reference to the global system controller used to create CLI sessions.
     * @param uds  Filesystem path for the Unix domain socket.
     */
    explicit WebSessionManager(core::Global& g, std::string uds)
        : global(g), api(), udsPath(std::move(uds))
    {
        api.setHandler([this](int fd, const nlohmann::json& msg, const UnixApi&) {
            this->onClientJson(fd, msg);
        });
        api.setOnClose([this](int fd, const UnixApi&) {
            this->onClientClosed(fd);
        });

        api.bindAndListen(udsPath, 64);
    }

    /**
     * @brief Runs the I/O and output-flush loop indefinitely.
     *
     * Each iteration calls pollOnce() to process socket events, then walks
     * every active session and flushes any pending CLI output back to the
     * corresponding client connection.
     *
     * @param timeoutMs  Maximum time in milliseconds to wait for socket activity per iteration.
     */
    void loop(int timeoutMs)
    {
        while (true)
        {
            api.pollOnce(timeoutMs);

            for (auto& kv : cid2ses)
            {
                const std::string& cid = kv.first;
                cli::CliSession* ses = kv.second;
                if (!ses) continue;

                auto itFd = cid2fd.find(cid);
                if (itFd == cid2fd.end()) continue;

                if (WebConsole* wc = getWebConsole(ses))
                {
                    wc->flushCommands(*ses, cid, false);
                }
            }
        }
    }

private:
    core::Global& global; ///< Global system controller; used to create CLI sessions via global.engine.
    UnixApi api;           ///< Unix socket server that delivers raw JSON messages to this manager.

    std::string udsPath; ///< Socket path passed to UnixApi::bindAndListen().
    std::string rmBuf;   ///< Scratch buffer reserved for future use.

    std::unordered_map<std::string, cli::CliSession*> cid2ses; ///< Maps client_id to its CLI session.
    std::unordered_map<std::string, int> cid2fd;               ///< Maps client_id to its current socket fd.
    std::unordered_map<int, std::string> fd2cid;               ///< Reverse map from socket fd to client_id; used on disconnect.

    /**
     * @brief Casts the ConsoleController of @p ses to a WebConsole, or returns nullptr.
     *
     * @param ses  CLI session whose controller is inspected.
     * @return     The WebConsole pointer if the controller is a WebConsole; otherwise nullptr.
     */
    static web::WebConsole* getWebConsole(cli::CliSession* ses)
    {
        if (!ses) return nullptr;
        cli::ConsoleController* ic = &ses->controller;
        return dynamic_cast<web::WebConsole*>(ic);
    }

    /**
     * @brief Records a bidirectional mapping between a client_id and a file descriptor.
     *
     * @param cid  Client identifier string from the JSON protocol.
     * @param fd   File descriptor of the connection carrying that client.
     */
    void bindCidToFd(const std::string& cid, int fd)
    {
        cid2fd[cid] = fd;
        fd2cid[fd] = cid;
    }

    /**
     * @brief Removes all mappings associated with @p fd without destroying the CLI session.
     *
     * Called when a connection drops so that a future reconnect with the same
     * client_id can be bound to the new fd.
     *
     * @param fd  File descriptor to unmap.
     */
    void unbindFd(int fd)
    {
        auto it = fd2cid.find(fd);
        if (it == fd2cid.end()) return;
        const std::string cid = it->second;
        fd2cid.erase(it);

        auto it2 = cid2fd.find(cid);
        if (it2 != cid2fd.end() && it2->second == fd)
            cid2fd.erase(it2);
    }

    /**
     * @brief Deletes the CLI session for @p cid and removes it from @ref cid2ses.
     *
     * @param cid  Client identifier whose session should be destroyed.
     */
    void destroySession(const std::string& cid)
    {
        auto it = cid2ses.find(cid);
        if (it == cid2ses.end()) return;
        if (it->second) delete it->second;
        cid2ses.erase(it);
    }

    /**
     * @brief Dispatches an inbound JSON message from @p fd based on its "action" field.
     *
     * Messages with a missing or empty "client_id", or a "type" other than
     * "router", are silently dropped. Valid actions are "subscribe", "unsubscribe",
     * and "cmd".
     *
     * @param fd   File descriptor the message arrived on.
     * @param msg  Parsed JSON object from the client.
     */
    void onClientJson(int fd, const nlohmann::json& msg)
    {
        std::cout << msg.dump(4) << std::endl;
        std::string action = msg.value("action", "");
        std::string type = msg.value("type", "");
        std::string cid = msg.value("client_id", "");

        if (cid.empty() || type != "router") return;

        if (action == "subscribe")
        {
            auto it = cid2ses.find(cid);
            if (it == cid2ses.end())
            {
                auto* wc = new WebConsole(api, fd, global);
                auto* ses = global.engine.createSession(wc);
                wc->beginSession();
                cid2ses[cid] = ses;
            }
            else
            {
                if (WebConsole* wc = getWebConsole(it->second))
                {
                    wc->beginSession();
                }
            }

            bindCidToFd(cid, fd);
            api.sendJson(fd, {
                {"type", "subscribe"},
                {"client_id", cid}
            });

            for (const auto& [cid, _] : cid2ses) {
                std::cout << cid << std::endl;
            }
            for (const auto& [cid, fd] : cid2fd) {
                std::cout << cid << " " << fd << std::endl;
            }
            for (const auto& [fd, cid] : fd2cid) {
                std::cout << fd << " " << cid << std::endl;
            }
            std::cout << "\n\n";


            return;
        }

        if (action == "unsubscribe")
        {
            unbindFd(fd);
            destroySession(cid);
            api.sendJson(fd, {
                {"type", "unsubscribe"},
                {"client_id", cid}
            });
            return;
        }

        if (action == "cmd")
        {
            auto it = cid2ses.find(cid);
            if (it == cid2ses.end() || !it->second) return;

            cli::CliSession* ses = it->second;

            const std::string line = msg.value("data", "");
            ses->handleInput(line);

            if (web::WebConsole* wc = getWebConsole(ses))
            {
                wc->flushCommands(*ses, cid, false);
            }
            return;
        }
    }

    /**
     * @brief Handles a disconnection event for @p fd.
     *
     * Removes the fd-to-cid mapping and destroys the associated CLI session
     * so resources are not held for clients that will never reconnect.
     *
     * @param fd  File descriptor of the connection that closed.
     */
    void onClientClosed(int fd)
    {
        auto it = fd2cid.find(fd);
        if (it == fd2cid.end()) return;

        std::string cid = it->second;
        unbindFd(fd);

        destroySession(cid);
    }
};

} // namespace web

#endif // WEB_SESSION_MANAGER_HPP
