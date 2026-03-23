// TcpEngine

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

class TcpEngine final
{
public:
    explicit TcpEngine(core::VirtualRouter& v, const Config& c = {});
    ~TcpEngine();

    TcpEngine(const TcpEngine&) = delete;
    TcpEngine& operator=(const TcpEngine&) = delete;

    TcpEngine(TcpEngine&&) = delete;
    TcpEngine& operator=(TcpEngine&&) = delete;

    Listener createListener(const TcpEndpoint& local, const ListenOptions& opts);
    Connection createConnection(const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opts);

    void closeListener(ListenId id) noexcept;
    void closeConnection(ConnId id) noexcept;

    // Listener-owned connection opts
    size_t listenerFlush(ListenId lid, ConnId cid) noexcept;
    void listenerDisconnect(ListenId id, ConnId cid) noexcept;

    size_t flush(ConnId cid) noexcept;
    void connectionDisconnect(ConnId cid);

    std::optional<TcpSocketKey> connectionSocketKey(ConnId id) const noexcept;

    size_t pollEvents(std::span<TcpEvent> outEvents, uint32_t timeoutMs) noexcept;
    size_t pump(Tcp& tcp, uint32_t timeoutMs, size_t maxEvents) noexcept;

private:
    struct ListenerState final
    {
        ListenId id{0};
        int fd{-1};
        int af{AF_UNSPEC};

        TcpEndpoint local{};
        size_t backlog{0};
        TcpSocketPolicy policyApplied{};
        size_t rxSize = 2048;

        AcceptCallback onAccept{nullptr};
        void* onAcceptUser{nullptr};

        ConnCallback acceptedConnCallback{nullptr};
        void* acceptedConnUser{nullptr};

        RecvCallback recvCallback{nullptr};
        void* recvUser{nullptr};

        std::vector<ConnId> accepted;
    };

    struct ConnectionState final
    {
        ConnectionState(size_t cid, TxBufferPool& pool, size_t recvBufSiz)
            : id(cid), bufferTx(pool.acquire()), bufferRx(cid, recvBufSiz) {}

        const ConnId id;
        int fd{-1};

        TcpSocketKey key{};
        ListenId ownerListener{0};

        bool connectPending{false};
        bool peerClosed{false};

        TcpError stickyError{};

        ConnCallback cb{nullptr};
        void* cbUser{nullptr};

        RecvCallback recvCb{nullptr};
        void* recvUser{nullptr};

        TxBuffer bufferTx;
        RxBuffer bufferRx;
    };

    static constexpr uint64_t kListenerTag = (1ull << 63);
    static constexpr uint64_t kIdMask = ~kListenerTag;

    static uint64_t packListener(ListenId id) noexcept { return kListenerTag | (id & kIdMask); }
    static uint64_t packConn(ConnId id) noexcept { return (id & kIdMask); }
    static bool isListenerTag(uint64_t v) noexcept { return (v & kListenerTag) != 0; }
    static uint64_t unpackId(uint64_t v) noexcept { return (v & kIdMask); }

private:
    TcpPort allocateEphemeral() noexcept;
    void epAdd(int fd, uint64_t tag, uint32_t events) noexcept;
    void epDel(int fd) noexcept;

    void bindToDeviceIfRequested(int fd, const TcpInterfaceBind& b);
    bool bindEndpoint(int fd, const TcpEndpoint& ep) noexcept;

    bool getLiveKey(int fd, TcpSocketKey& out) const noexcept;
    TcpState linuxState(int fd) const;

    ConnId adoptAcceptedSocket(ListenerState& lst, int cfd);
    size_t acceptLoop(ListenerState& lst, Tcp* tcp, std::span<TcpEvent> acceptEvents, size_t& produced, bool invokeCallbacks) noexcept;

    void dispatchConnectEvent(Tcp& tcp, ConnId cid, TcpEventType t, TcpError e) noexcept;

    ConnectionState* getConnection(ConnId cid) noexcept;
    ConnectionState* getAcceptedConnectionChecked(ListenId lid, ConnId cid) noexcept;

    void closeConnectionInternal(ConnId cid) noexcept;

private:
    core::VirtualRouter& vr;
    Config cfg;
    TxBufferPool bufferPool;

    int epfd{-1};

    ListenId nextListenId{1};
    ConnId nextConnId{1};
    TcpPort nextEphemeral{0};

    std::unordered_map<ListenId, ListenerState> listeners;
    std::unordered_map<ConnId, ConnectionState> connections;

    std::vector<epoll_event> epScratch;
    std::vector<uint8_t> ioScratch;
};
} // namespace transport::tcp

#endif // TCP_ENGINE_H

