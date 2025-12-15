// Tcp.h

#ifndef TCP_H
#define TCP_H

#include "TcpConnection.h"
#include "TcpListener.h"
#include "TcpOutput.h"
#include "TcpTimeWait.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>

class VirtualRouter;
class Interface;

namespace TCP
{
class TcpStack
{
public:
    using ConnId = uint64_t;
    using ListenId = uint64_t;

    struct Config final
    {
        Config() : connectionDefaults({}) {}
        TcpPort ephemeralMin{49152};
        TcpPort ephemeralMax{65535};
        size_t maxConnections{4096};
        TcpConnection::Config connectionDefaults;
    };

    explicit TcpStack(VirtualRouter& vrf, Config cfg = {});

    uint64_t listen(const TcpEndpoint& local, size_t backlog = 128);
    void unlisten(ListenId id);

    std::optional<ConnId> accept(uint64_t id);
    
    uint64_t connect(const TcpEndpoint& local, const TcpEndpoint& remote, Interface& iface);

    size_t send(ConnId id, std::span<const uint8_t> data);
    size_t recv(ConnId id, std::span<uint8_t> out);

    void shutdown(ConnId id, TcpShutdown how);
    void close(ConnId id);

    TcpState state(ConnId id) const;
    std::optional<TcpSocketKey> socketKey(ConnId id) const;

    void input(const TcpSegment& seg);

private:
    struct ConnEntry final
    {
        ConnId id{0};
        TcpConnection conn;
    };

    struct ListenerEntry final
    {
        ListenId listendId{0};
        TcpListener listener;
    };

    ConnId allocateConnId() noexcept;
    ListenId allocateListenId() noexcept;
    TcpPort allocateEphemeralPort(const IPAddress& localAddr);

    ConnEntry* findConnByKey(const TcpSocketKey& key) noexcept;
    const ConnEntry* findConnByKey(const TcpSocketKey& key) const noexcept;

    ListenerEntry* findListenerForLocal(const TcpEndpoint& local) noexcept;
    const ListenerEntry* findListenerForLocal(const TcpEndpoint& local) const noexcept;

    ConnEntry* findConnById(ConnId id) noexcept;
    const ConnEntry* findConnById(ConnId id) const noexcept;

    ListenerEntry* findListenerById(ListenId id) noexcept;
    const ListenerEntry* findListenerById(ListenId id) const noexcept;

    ConnId createChildForSyn(ListenerEntry& lst, const TcpSegment& syn);

    void reapClosed();

private:
    Config cfg{};
    VirtualRouter& vrf;

    TcpOutput output;
    TcpTimeWait timeWait;

    ConnId nextConnId{1};
    ListenId nextListenId{1};
    TcpPort nextEphemeral{0};

    std::unordered_map<ConnId, ConnEntry> conns;
    std::unordered_map<ListenId, ListenerEntry> listeners;

    std::unordered_map<TcpSocketKey, ConnId> keyToConn;

};
} // namespace TCP

#endif // TCP_H
