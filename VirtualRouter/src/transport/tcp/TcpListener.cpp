// TcpListener.cpp

#include "TcpListener.h"

namespace TCP {

TcpListener::TcpListener(TcpEndpoint local, Interface* iface, std::size_t backlog)
    : local(std::move(local)), backlog(backlog), iface(iface) {}

const TcpEndpoint& TcpListener::localEndpoint() const noexcept { return local; }
std::size_t TcpListener::getBacklog() const noexcept { return backlog; }

bool TcpListener::canAdmitHalfOpen() const noexcept
{
    return halfOpen.size() < backlog;
}

bool TcpListener::admitHalfOpen(const TcpSocketKey& childKey)
{
    if (!canAdmitHalfOpen()) return false;
    return halfOpen.insert(childKey).second;
}

void TcpListener::removeHalfOpen(const TcpSocketKey& childKey)
{
    halfOpen.erase(childKey);
}

void TcpListener::enqueueEstablished(const TcpSocketKey& childKey)
{
    // Remove from half-open tracking if present and add to accept queue.
    halfOpen.erase(childKey);
    acceptQ.push_back(childKey);
}

std::optional<TcpSocketKey> TcpListener::accept()
{
    if (acceptQ.empty()) return std::nullopt;
    TcpSocketKey k = acceptQ.front();
    acceptQ.pop_front();
    return k;
}

} // namespace tcp
