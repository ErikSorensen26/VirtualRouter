// Listener.h

#ifndef TCP_LISTENER_H
#define TCP_LISTENER_H

#include "TcpTypes.hpp"

namespace TCP
{
class Connection;
class TcpEngine;

class Listener final
{
public:
    Listener() = default;
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    Listener(Listener&& other) noexcept;
    Listener& operator=(Listener&& other) noexcept;

    bool ok() const noexcept { return engine && id != 0; }
    explicit operator bool() const noexcept { return ok(); }

    ListenId getId() const noexcept { return id; }

    size_t send(ConnId cid, std::span<const uint8_t>& data) noexcept;

    void shutdown() noexcept;

    void disconnect(ConnId cid) noexcept;

private:
    friend class TcpEngine;
    friend class Connection;

    Listener(TcpEngine* e, ListenId lid) noexcept
        : engine(e), id(lid) {}

    TcpEngine* engine{nullptr};
    ListenId id{0};
};
}

#endif
