// TcpSocketKey

#ifndef TCP_SOCKET_KEY_HPP
#define TCP_SOCKET_KEY_HPP

#include "TcpEndpoint.hpp"
#include <cstdint>
#include <functional>

namespace TCP
{
struct TcpSocketKey final
{
    TcpEndpoint local{};
    TcpEndpoint remote{};

    TcpSocketKey flipped() const noexcept { return TcpSocketKey{remote, local}; }
    bool matchListener(const TcpEndpoint& listenLocal) const noexcept
    {
        if (local.port != listenLocal.port) return false;
        if (listenLocal.address.isUnspecified()) return true;
        return local.address == listenLocal.address;
    }

    bool operator==(const TcpSocketKey& other) noexcept
    {
        return local == other.local && remote == other.remote;
    }

    bool operator!=(const TcpSocketKey& other) noexcept
    {
        return local != other.local || remote != other.remote;
    }
};
}

namespace std
{
template <>
struct hash<TCP::TcpSocketKey>
{
    size_t operator()(const TCP::TcpSocketKey& k) const noexcept
    {
        size_t h1 = std::hash<TCP::TcpEndpoint>{}(k.local);
        size_t h2 = std::hash<TCP::TcpEndpoint>{}(k.remote);

        h2 ^= h1 + 0x9e3779b7f4a7c15ull + (h2 << 6) + (h2 >> 2);

        return h2;
    }
};
}

#endif // TCP_Socket_KEY_HPP
