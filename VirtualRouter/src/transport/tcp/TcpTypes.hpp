// TcpTypes.hpp

#ifndef TCP_TYPES_HPP
#define TCP_TYPES_HPP

#include <cstdint>
#include <cstddef>
#include <optional>
#include <functional>

#include <IPAddress.hpp>
#include <AddressFamily.hpp>

namespace TCP
{
using TcpPort = uint16_t;

enum class TcpState : uint8_t
{
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    CLOSE_WAIT, 
    CLOSING,
    LAST_ACK,
    TIME_ACK,
    TIME_WAIT
};

enum class TcpShutdown : uint8_t
{
    READ,
    WRITE,
    READ_WRITE
};

enum class TcpErrc : uint8_t
{
    OK = 0,

    INVALID_ARGUMENT,
    NOT_FOUND,
    NO_RESOURCES,
    NOT_SUPPORTED,
    PERMISSION,

    WOULD_BLOCK,
    IN_PROGRESS,

    TIMED_OUT,
    CONNECTION_REFUSED,
    CONNECTION_RESET,
    NOT_CONNECTED,
    BROKEN_PIPE,

    ADDRESS_IN_USE,
    ADDRESS_NOT_AVAILABLE,

    NETWORK_UNREACHABLE,
    HOST_UNREACHABLE,

    SYSTEM_ERROR
};

struct TcpError final
{
    TcpErrc code{TcpErrc::OK};
    int osErrno{0};

    constexpr bool ok() const noexcept { return code == TcpErrc::OK; }
};

template <typename T>
struct TcpResult final
{
    T value{};
    TcpError error{};

    constexpr bool ok() const noexcept { return error.code == TcpErrc::OK; }
    constexpr explicit operator bool() const noexcept { return ok(); }
};

template <>
struct TcpResult<void> final
{
    TcpError error{};
    constexpr bool ok() const noexcept { return error.code == TcpErrc::OK; }
    constexpr explicit operator bool() const noexcept { return ok(); }
};

struct TcpEndpoint final
{
    IPAddress address{};
    TcpPort port{0};

    bool isWildcardAddress() const noexcept
    {
        return address.isUnspecified();
    }

    bool operator==(const TcpEndpoint& other) const noexcept
    {
        return address == other.address && port == other.port;
    }

    bool operator!=(const TcpEndpoint& other) const noexcept
    {
        return !(*this == other);
    }
};

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

    bool operator==(const TcpSocketKey& other) const noexcept
    {
        return local == other.local && remote == other.remote;
    }

    bool operator!=(const TcpSocketKey& other) const noexcept
    {
        return !(*this == other);
    }
};

struct TcpSocketPolicy final
{
    std::optional<uint8_t> ttl{};   // Hop limit / TTL
    std::optional<uint8_t> tos{};   // DSCP/ENC byte (TOS/TCLASS)
    bool lowLatency{false};         // minimize latency (NODELAY)
    bool keepAlive{false};          // keep alives
};

struct TcpInterfaceBind final
{
    const char* ifname{nullptr};
    uint32_t ifnameLen{0};
};

enum class TcpEventType : uint8_t
{
    ACCEPT_READY,   // listener hnas 1+ pending accepts
    ACCEPTED,       // new server-side accepted connection
    CONNECTED,      // connect completed successfully
    READABLE,       // recv would not block
    WRITABLE,       // send would not block
    PEER_CLOSED,    // remote performed orderly shutdown
    ERROR           // connection has an error
};

struct TcpEvent final
{
    TcpEventType type{};
    uint64_t id{0};
    TcpError error{};
};

struct TcpIpAdapter final
{
    static int af(const IPAddress& ip) noexcept
    {
        if (ip.isV6)
            return AF_INET6;
        else
            return AF_INET;
    }

    static bool unspecified(const IPAddress& ip) noexcept
    {
        return ip.isUnspecified();
    }

    static void writeSocketaddr(const IPAddress& ipIn, TcpPort portIn, void* sockaddrOut, uint32_t* socklenOut) noexcept
    {
        auto* ss = reinterpret_cast<sockaddr_storage*>(sockaddrOut);
        std::memset(ss, 0, sizeof(*ss));

        if (af(ipIn) == AF_INET)
        {
            sockaddr_in sin{};
            sin.sin_family = AF_INET;
            sin.sin_port = htons(portIn);

            std::memcpy(&sin.sin_addr, ipIn.raw, 4);

            std::memcpy(ss, &sin, sizeof(sin));
            *socklenOut = sizeof(sockaddr_in);
        }

        sockaddr_in6 sin6{};
        sin6.sin6_family = AF_INET6;
        sin6.sin6_port = htons(portIn);

        std::memcpy(&sin6.sin6_addr, ipIn.raw, 16);

        std::memcpy(ss, &sin6, sizeof(sin6));
        *socklenOut = sizeof(sockaddr_in6);
    }

    static void readSockaddr(const void* sockaddrIn, uint32_t socklenIn, IPAddress& ipOut, TcpPort& portOut) noexcept
    {
        (void)socklenIn;

        const auto* sa = reinterpret_cast<const sockaddr*>(sockaddrIn);

        if (sa->sa_family == AF_INET)
        {
            const auto* sin = reinterpret_cast<const sockaddr_in*>(sa);
            portOut = ntohs(sin->sin_port);

            std::memcpy(ipOut.raw, &sin->sin_addr, 4);
            return;
        }

        if (sa->sa_family == AF_INET6)
        {
            const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(sa);
            portOut = ntohs(sin6->sin6_port);

            std::memcpy(ipOut.raw, &sin6->sin6_addr, 16);
            ipOut.isV6 = true;
            return;
        }
    }
};
}

namespace std
{
template <>
struct hash<TCP::TcpEndpoint>
{
    size_t operator()(const TCP::TcpEndpoint& k) const noexcept
    {
        size_t h = std::hash<IPAddress>{}(k.address);
        size_t p = static_cast<size_t>(k.port);

        p ^= h + 0x9e3779b7f4a7c15ull + (p << 6) + (p >> 2);
        return p;
    }
};

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

#endif // TCP_TYPES_HPP
