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
        engine->closeListener(id);

    engine = other.engine;
    id = other.id;

    other.engine = nullptr;
    other.id = 0;
    return *this;
}

void Listener::shutdown() noexcept
{
    if (!engine || id == 0) return;

    engine->closeListener(id);
    engine = nullptr;
    id = 0;
}

void Listener::disconnect(ConnId cid) noexcept
{
    return engine->listenerDisconnect(id, cid);
}
} // namespace transport::tcp
