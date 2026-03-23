// Listener.cpp

#include <cassert>

#include "Listener.h"
#include "Connection.h"
#include "TcpEngine.h"

namespace transport::tcp
{
Listener::~Listener()
{
    if (!engine || id == 0) return;
    engine->closeListener(id);
}

Listener::Listener(Listener&& other) noexcept
    : engine(other.engine), id(other.id)
{
    other.engine = nullptr;    
    other.id = 0;
}

Listener& Listener::operator=(Listener&& other) noexcept
{
    if (this == &other) return *this;

    if (engine && id != 0)
        engine->closeConnection(id);

    engine = other.engine;
    id = other.id;

    other.engine = nullptr;
    other.id = 0;
    return *this;
}

size_t Listener::send(ConnId cid, std::span<const uint8_t>& data) noexcept
{
    (void)cid; (void)data;
    return 0;
}

void Listener::shutdown() noexcept
{
    if (!engine || id != 0) return;

    engine->closeListener(id);
}

void Listener::disconnect(ConnId cid) noexcept
{
    return engine->listenerDisconnect(id, cid);
}
} // namespace transport::tcp
