// Connection.cpp

#include "Connection.h"
#include "TcpEngine.h"

namespace TCP
{
Connection::~Connection()
{
    if (!engine || id == 0) return;
    engine->closeConnection(id);
}

Connection::Connection(Connection&& other) noexcept
    : bufferTx(other.bufferTx), engine(other.engine), id(other.id)
{
    other.engine = nullptr;
    other.id = 0;
}

Connection& Connection::operator=(Connection&& other) noexcept
{
    if (this == &other) return *this;

    if (engine && id != 0)
        engine->closeConnection(id);

    engine = other.engine;
    id = other.id;

    other.engine = nullptr;
    other.engine = 0;
    return *this;
}

size_t Connection::flush() noexcept
{
    return engine->flush(id);
}

void Connection::disconnect() noexcept
{
    if (!engine || id == 0) return;

    engine->closeConnection(id);
    engine = nullptr;
    id = 0;
}

std::optional<TcpSocketKey> Connection::socketKey() const noexcept
{
    if (!engine || id == 0)
        return std::nullopt;
    return engine->connectionSocketKey(id);
}
}
