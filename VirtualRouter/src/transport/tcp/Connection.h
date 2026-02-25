// Connection.h

#ifndef TCP_CONNECTION_H
#define TCP_CONNECTION_H

#include "TcpTypes.hpp"

namespace TCP
{
class Listener;
class TcpEngine;
class TxBuffer;

class Connection final
{
public:
    ~Connection();

    Connection(const Connection& other) = delete;
    Connection& operator=(const Connection& other) = delete;

    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    bool ok() const noexcept { return engine && id != 0; }
    explicit operator bool() const noexcept { return ok(); }

    ConnId getId() const noexcept { return id; }

    size_t flush() noexcept;

    void disconnect() noexcept;

    std::optional<TcpSocketKey> socketKey() const noexcept;

private:
    friend class RxConsumer;
    friend class TcpEngine;
    friend class Listener;

    Connection(TcpEngine* e, ConnId cid, TxBuffer& bufTx)
        : bufferTx(bufTx), engine(e), id(cid) {}

    TxBuffer& bufferTx;

    TcpEngine* engine{nullptr};
    ConnId id{0};
};
}

#endif // TCP_CONNECTION_H
