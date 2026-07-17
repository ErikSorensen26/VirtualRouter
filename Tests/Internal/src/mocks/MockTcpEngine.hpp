// MockTcpEngine.hpp

#ifndef MOCK_TCP_ENGINE_HPP
#define MOCK_TCP_ENGINE_HPP

#include <gmock/gmock.h>

#include <algorithm>
#include <mutex>
#include <vector>
#include <cstring>

#include <VirtualRouter.h>
#include "tcp/TcpEngine.h"
#include "tcp/Tcp.h"
#include "tcp/rx/RxConsumer.h"

namespace transport::tcp
{
/**
 * @brief In-process fake of @ref TcpEngine that pairs connect()/listen() calls by 4-tuple instead of touching real sockets.
 *
 * `MockTcpEngine` overrides the three socket-facing entry points
 * (`createListener`, `createConnection`, `pump`) with fakes that never call
 * into the kernel. Listener registration is tracked in a process-wide
 * registry (@ref globalListenerRegistry) so that a connect on one engine
 * instance can be matched against a listener on another, letting tests wire
 * up two independent `Tcp`/`TcpEngine` pairs (e.g. client and server) and
 * exchange real bytes through the normal `TxBuffer`/`RxBuffer` path without
 * any actual I/O.
 *
 * @ref fakePump drives the whole simulation: it completes any pending
 * connects (pairing them with a matching listener) and, for every
 * connection with pending TX data, drains it straight into the paired
 * connection's RX buffer and fires its recv callback synchronously.
 *
 * Connections created here have `ConnectionState::fd == -1`; the real
 * `TcpEngine` methods (`flush`, `read`, `shutdownConnection`,
 * `connectionState`, `connectionSocketKey`) all special-case `fd < 0` to
 * skip socket syscalls, so a `Connection`/`Listener` handle behaves the same
 * whether it is backed by a real or a mock engine.
 */
class MockTcpEngine : public TcpEngine
{
public:
    /// Constructs the mock and wires gmock's ON_CALL/EXPECT_CALL defaults to the fake implementations below.
    explicit MockTcpEngine(core::VirtualRouter& v, const Config& c = {})
        : TcpEngine(v, c)
    {
        using ::testing::_;
        using ::testing::Invoke;
        using ::testing::AnyNumber;

        ON_CALL(*this, createListener).WillByDefault(Invoke(this, &MockTcpEngine::fakeCreateListener));
        EXPECT_CALL(*this, createListener).Times(AnyNumber());

        ON_CALL(*this, createConnection).WillByDefault(Invoke(this, &MockTcpEngine::fakeCreateConnection));
        EXPECT_CALL(*this, createConnection).Times(AnyNumber());

        ON_CALL(*this, pump).WillByDefault(Invoke(this, &MockTcpEngine::fakePump));
        EXPECT_CALL(*this, pump).Times(AnyNumber());
    }

    /// Removes all of this engine's listeners from the global registry so later engines don't match against a dead one.
    ~MockTcpEngine() override
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        auto& reg = globalListenerRegistry();
        reg.erase(std::remove_if(reg.begin(), reg.end(),
            [this](const ListenerEntry& e) { return e.engine == this; }), reg.end());
    }

    MOCK_METHOD(Listener, createListener, (const TcpEndpoint& local, const ListenOptions& opts), (override));
    MOCK_METHOD(Connection, createConnection, (const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opts), (override));
    MOCK_METHOD(size_t, pump, (Tcp& tcp, uint32_t timeoutMs, size_t maxEvents), (noexcept, override));

private:
    /// One registered listener, tracked so a connect() on any MockTcpEngine can find it by endpoint.
    struct ListenerEntry
    {
        MockTcpEngine* engine; ///< Owning engine; used to remove the entry on destruction and to reach its connection map.
        ListenId id;           ///< Listener id within `engine->listeners`.
        TcpEndpoint local;     ///< Bound local endpoint, matched against a connecting peer's remote endpoint.
    };

    /// Guards `globalListenerRegistry()`; separate mutex because the registry is shared across independent MockTcpEngine instances/threads.
    static std::mutex& registryMutex()
    {
        static std::mutex m;
        return m;
    }

    /// Process-wide listener directory spanning all MockTcpEngine instances, so connects can pair across separate engines (e.g. client vs. server in a test).
    static std::vector<ListenerEntry>& globalListenerRegistry()
    {
        static std::vector<ListenerEntry> reg;
        return reg;
    }

    /// Fake behind `createListener`: registers the listener locally and in the global registry, without opening a real socket.
    Listener fakeCreateListener(const TcpEndpoint& local, const ListenOptions& opts)
    {
        ListenId id = nextListenId++;

        ListenerState lst{};
        lst.id = id;
        lst.local = local;
        lst.backlog = opts.backlog;
        lst.rxSize = opts.rxBufferSize;
        lst.onAccept = opts.onAccept;
        lst.onAcceptUser = opts.onAcceptUser;
        lst.acceptedConnCallback = opts.acceptConnCallback;
        lst.acceptedConnUser = opts.acceptedConnUser;
        lst.recvCallback = opts.recvCallback;
        lst.recvUser = opts.recvUser;

        listeners.emplace(id, std::move(lst));

        {
            std::lock_guard<std::mutex> lock(registryMutex());
            globalListenerRegistry().push_back(ListenerEntry{this, id, local});
        }

        return makeListenerHandle(id);
    }

    /// Fake behind `createConnection`: registers a pending outbound connection with no socket (`fd` stays -1); paired later by @ref completePendingConnects.
    Connection fakeCreateConnection(const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opts)
    {
        ConnId cid = nextConnId++;

        auto cit = connections.try_emplace(cid, cid, bufferPool, opts.rxBufferSize);
        ConnectionState& c = cit.first->second;

        c.key.local = local;
        c.key.remote = remote;
        c.cb = opts.callback;
        c.cbUser = opts.callbackUser;
        c.recvCb = opts.recvCallback;
        c.recvUser = opts.recvUser;
        c.connectPending = true;

        return makeConnectionHandle(cid, c.bufferTx);
    }

    /**
     * @brief Pairs any pending outbound connect() with a matching listener and fires its callbacks.
     *
     * For each connection still marked `connectPending`, looks up a listener
     * bound to the connection's remote endpoint (possibly on a different
     * `MockTcpEngine`), synthesizes the accepted-side `ConnectionState` on
     * that peer engine, and fires the connecting side's `CONNECTED` callback
     * plus the listening side's `onAccept` callback.
     */
    void completePendingConnects(Tcp& tcp) noexcept
    {
        for (auto& [cid, c] : connections)
        {
            if (!c.connectPending) continue;

            MockTcpEngine* peerEngine = nullptr;
            ListenId peerListenId = 0;
            {
                std::lock_guard<std::mutex> lock(registryMutex());
                TcpSocketKey candidate{c.key.remote, c.key.local};
                for (const auto& entry : globalListenerRegistry())
                {
                    if (candidate.matchListener(entry.local))
                    {
                        peerEngine = entry.engine;
                        peerListenId = entry.id;
                        break;
                    }
                }
            }
            if (!peerEngine) continue;

            auto lit = peerEngine->listeners.find(peerListenId);
            if (lit == peerEngine->listeners.end()) continue;
            ListenerState& lst = lit->second;

            ConnId peerCid = peerEngine->nextConnId++;
            auto pit = peerEngine->connections.try_emplace(peerCid, peerCid, peerEngine->bufferPool, lst.rxSize);
            ConnectionState& peerConn = pit.first->second;

            peerConn.key.local = c.key.remote;
            peerConn.key.remote = c.key.local;
            peerConn.ownerListener = lst.id;
            peerConn.cb = lst.acceptedConnCallback;
            peerConn.cbUser = lst.acceptedConnUser;
            peerConn.recvCb = lst.recvCallback;
            peerConn.recvUser = lst.recvUser;

            lst.accepted.push_back(peerCid);
            c.connectPending = false;

            if (c.cb)
            {
                TcpEvent ev{TcpEventType::CONNECTED, cid, {}};
                ConnCallbackCtx ctx{c.cbUser, tcp, cid, ev, c.key};
                c.cb(ctx);
            }

            if (lst.onAccept)
            {
                Connection newConn = peerEngine->makeConnectionHandle(peerCid, peerConn.bufferTx);
                AcceptCallbackCtx actx{lst.onAcceptUser, tcp, lst.id, newConn, peerConn.key};
                lst.onAccept(actx);
            }
        }
    }

    /**
     * @brief Finds the other side of @p c's pairing by matching the swapped 4-tuple.
     *
     * Searches this engine's own connections first, then every other
     * registered engine's connections, since the peer may live on a
     * different `MockTcpEngine`.
     *
     * @return The peer's `ConnectionState` and owning engine, or `{nullptr, nullptr}` if not found.
     */
    std::pair<ConnectionState*, MockTcpEngine*> findPeer(ConnectionState& c) noexcept
    {
        for (auto& [pid, pc] : connections)
        {
            if (pid == c.id) continue;
            if (pc.key.local == c.key.remote && pc.key.remote == c.key.local)
                return {&pc, this};
        }

        std::lock_guard<std::mutex> lock(registryMutex());
        for (const auto& entry : globalListenerRegistry())
        {
            if (entry.engine == this) continue;
            for (auto& [pid, pc] : entry.engine->connections)
            {
                if (pc.key.local == c.key.remote && pc.key.remote == c.key.local)
                    return {&pc, entry.engine};
            }
        }
        return {nullptr, nullptr};
    }

    /// Drains @p c's TX buffer into its paired connection's RX buffer (via @ref findPeer) and fires the peer's recv callback.
    void deliverPending(Tcp& tcp, ConnectionState& c) noexcept
    {
        auto [peer, peerEngine] = findPeer(c);
        if (!peer || !peerEngine) return;

        std::vector<uint8_t> scratch;
        while (!c.bufferTx.empty())
        {
            auto span = c.bufferTx.peek(0);
            if (span.empty()) break;
            size_t off = scratch.size();
            scratch.resize(off + span.size());
            std::memcpy(scratch.data() + off, span.data(), span.size());
            c.bufferTx.consume(span.size());
        }
        if (scratch.empty()) return;

        RxConsumer consumer = peer->bufferRx.consume(std::span<uint8_t>(scratch));
        if (peer->recvCb)
        {
            RecvCallbackCtx ctx{peer->recvUser, tcp, peer->id, consumer, peer->key};
            peer->recvCb(ctx);
        }
    }

    /// Fake behind `pump`: completes pending connects, then delivers any buffered TX data to its peer for every connection. Ignores timeoutMs/maxEvents since there is no real epoll wait.
    size_t fakePump(Tcp& tcp, uint32_t /*timeoutMs*/, size_t /*maxEvents*/) noexcept
    {
        completePendingConnects(tcp);

        size_t processed = 0;
        for (auto& [cid, c] : connections)
        {
            if (!c.bufferTx.empty())
            {
                deliverPending(tcp, c);
                ++processed;
            }
        }
        return processed;
    }
};

} // namespace transport::tcp

#endif // MOCK_TCP_ENGINE_HPP
