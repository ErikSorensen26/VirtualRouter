// Tcp.h

#ifndef TCP_H
#define TCP_H

#include <cstdint>
#include <span>

#include "TcpTypes.hpp"
#include "Listener.h"
#include "Connection.h"

class VirtualRouter;
class TcpSegment;

namespace TCP
{
class TcpEngine;

class Tcp
{
public:
    explicit Tcp(VirtualRouter& vrf, Config cfg = {});
    ~Tcp();

    Tcp(const Tcp&) = delete;
    Tcp& operator=(const Tcp&) = delete;

    Tcp(Tcp&&) = delete;
    Tcp& operator=(Tcp&&) = delete;

    Listener listen(const TcpEndpoint& local, const ListenOptions& opt = {});
    Connection connect(const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opt = {});

    void close(ConnId id);

    size_t pollEvents(std::span<TcpEvent> outEvents, uint32_t timeoutMs = 0);
    size_t pump(uint32_t timeoutMs = 0, size_t maxEvents = 64);

    // Future
    void input(const TcpSegment& seg);

private:
    friend class Listener;
    friend class Connection;

    TcpEngine* engine{nullptr};
};

} // namespace TCP

#endif // TCP_H
