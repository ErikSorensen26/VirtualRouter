// TcpListener.h

#ifndef TCP_LISTENER_H
#define TCP_LISTENER_H

#include <TcpEndpoint.hpp>
#include <TcpSocketKey.hpp>

#include <cstddef>
#include <deque>
#include <optional>
#include <unordered_set>

#include <Interface.h>

namespace TCP
{
class TcpListener
{
public:
    TcpListener(TcpEndpoint local, Interface* iface, size_t backlog);

    const TcpEndpoint& localEndpoint() const noexcept;
    size_t getBacklog() const noexcept;

    bool canAdmitHalfOpen() const noexcept;
    bool admitHalfOpen(const TcpSocketKey& childKey);
    void removeHalfOpen(const TcpSocketKey& childKey);

    void enqueueEstablished(const TcpSocketKey& childKey);
    std::optional<TcpSocketKey> accept();

private:

    TcpEndpoint local{};
    size_t backlog{0};

    std::atomic<Interface*> iface;

    std::unordered_set<TcpSocketKey> halfOpen;
    std::deque<TcpSocketKey> acceptQueue;
};
} // namespace TCP

#endif // TCP_LISTENER_H
