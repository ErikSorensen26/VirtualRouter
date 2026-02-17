// Tcp.cpp

#include "Tcp.h"
#include "TcpEngine.h"

namespace TCP
{
Tcp::Tcp(VirtualRouter& vrf, Config cfg)
{
    engine = new TcpEngine(vrf, cfg);
}

Tcp::~Tcp()
{
    delete engine;
    engine = nullptr;
}

Listener Tcp::listen(const TcpEndpoint& local, const ListenOptions& opt)
{
    return engine->createListener(local, opt);
}

Connection Tcp::connect(const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opt)
{
    return engine->createConnection(local, remote, opt);
}

void Tcp::close(ConnId id)
{
    engine->closeConnection(id);
}

size_t Tcp::pollEvents(std::span<TcpEvent> outEvents, uint32_t timeoutMs)
{
    return engine->pollEvents(outEvents, timeoutMs);
}

size_t Tcp::pump(uint32_t timeoutMs, size_t maxEvents)
{
    return engine->pump(*this, timeoutMs, maxEvents);
}

void Tcp::input(const TcpSegment&)
{
    // TODO
}

} // namespace TCP
