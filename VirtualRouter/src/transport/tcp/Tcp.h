// Tcp.h

#ifndef TCP_H
#define TCP_H

#include <optional>
#include <cstdint>
#include <span>

#include "TcpTypes.hpp"

class VirtualRouter;
class TcpSegment;

namespace TCP
{
class Tcp
{
public:
    using ConnId = uint64_t;
    using ListenId = uint64_t;

    using ConnCallback = void(*)(void* user, Tcp& tcp, ConnId id, const TcpEvent& ev) noexcept;
    using AcceptCallback = void(*)(void* user, Tcp& tcp, ListenId lid, ConnId newConn, const TcpSocketKey& key) noexcept;

    struct Config final
    {
        Config() {};
        TcpPort ephemeralMin{49152};
        TcpPort ephemeralMax{65535};
        size_t maxConnections{4096};

        TcpSocketPolicy defaults{};
    };

    struct ListenOptions final
    {
        ListenOptions() {}
        TcpSocketPolicy policy{};
        TcpInterfaceBind bind{};
        size_t backlog{128};

        AcceptCallback onAccept{nullptr};
        void* onAcceptUser{nullptr};

        ConnCallback acceptConnCallback{nullptr};
        void* acceptedConnUser{nullptr};
    };

    struct ConnectOptions final
    {
        ConnectOptions() {}
        TcpSocketPolicy policy{};
        TcpInterfaceBind bind{};

        ConnCallback callback{nullptr};
        void* callbackUser{nullptr};
    };

    explicit Tcp(VirtualRouter& vrf, Config cfg = {});
    ~Tcp();

    Tcp(const Tcp&) = delete;
    Tcp& operator=(const Tcp&) = delete;

    TcpResult<ListenId> listen(const TcpEndpoint& local, const ListenOptions& opt = {});
    TcpResult<void> unlisten(ListenId id);

    TcpResult<ConnId> accept(ListenId id);

    TcpResult<ConnId> connect(const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opt = {});

    TcpResult<size_t> send(ConnId id, std::span<const uint8_t> data);
    TcpResult<size_t> recv(ConnId id, std::span<uint8_t> out);

    TcpResult<void> shutdown(ConnId id, TcpShutdown how);
    TcpResult<void> close(ConnId id);

    TcpResult<TcpState> state(ConnId id) const;
    TcpResult<std::optional<TcpSocketKey>> socketKey(ConnId id) const;

    TcpResult<size_t> pollEvents(std::span<TcpEvent> outEvents, uint32_t timeoutMs = 0);
    TcpResult<size_t> pump(uint32_t timeoutMs = 0, size_t maxEvents = 64);

    TcpResult<void> setCallback(ConnId, ConnCallback cb, void* user = nullptr);

    // Future
    TcpResult<void> input(const TcpSegment& seg);

private:
    struct TcpEngine;
    TcpEngine* engine{nullptr};
};
}

#endif // TCP_H
