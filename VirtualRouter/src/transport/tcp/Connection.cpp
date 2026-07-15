// Connection.cpp

#include <cstring>

#include "Connection.h"
#include "TcpEngine.h"
#include "tx/TxBuffer.h"

namespace transport::tcp
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
    other.id = 0;
    return *this;
}

std::span<uint8_t> Connection::reserveSpan(size_t minBytes) noexcept
{
    return bufferTx.reserveSpan(minBytes);
}

void Connection::commit(size_t n) noexcept
{
    bufferTx.commit(n);
}

size_t Connection::write(std::span<const uint8_t> data) noexcept
{
    size_t buffered = 0;
    while (buffered < data.size())
    {
        auto span = bufferTx.reserveSpan(1);
        if (span.empty()) break; // pool exhausted
        size_t n = std::min(span.size(), data.size() - buffered);
        std::memcpy(span.data(), data.data() + buffered, n);
        bufferTx.commit(n);
        buffered += n;
    }
    return buffered;
}

size_t Connection::read(std::span<uint8_t> out) noexcept
{
    if (!engine || id == 0) return 0;
    return engine->read(id, out);
}

void Connection::shutdown(TcpShutdown how) noexcept
{
    if (!engine || id == 0) return;
    engine->shutdownConnection(id, how);
}

TcpState Connection::state() const noexcept
{
    if (!engine || id == 0) return TcpState::CLOSED;
    return engine->connectionState(id);
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
} // namespace transport::tcp
