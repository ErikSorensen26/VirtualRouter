// TcpTypes.hpp

#ifndef TCP_TYPES_HPP
#define TCP_TYPES_HPP

#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <functional>

#include <IPAddress.h>
#include <AddressFamily.hpp>
#include <ByteUtils.hpp>

namespace transport
{

namespace tcp
{
class Connection;
class RxConsumer;
class Tcp;

using TcpPort = uint16_t;
using ConnId = uint64_t;
using ListenId = uint64_t;

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
    SUCCESS = 0,

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
    TcpErrc code{TcpErrc::SUCCESS};
    int osErrno{0};

    constexpr bool ok() const noexcept { return code == TcpErrc::SUCCESS; }
};

/*template <typename T>
struct TcpResult final
{
    T value{};
    TcpError error{};

    constexpr bool ok() const noexcept { return error.code == TcpErrc::SUCCESS; }
    constexpr explicit operator bool() const noexcept { return ok(); }
};

template <>
struct TcpResult<void> final
{
    TcpError error{};
    constexpr bool ok() const noexcept { return error.code == TcpErrc::SUCCESS; }
    constexpr explicit operator bool() const noexcept { return ok(); }
};*/

struct TcpEndpoint final
{
    types::IPAddress address{};
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
    bool pathMtuDiscovery{false};   // set DF bit / IP_MTU_DISCOVER
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

struct ConnCallbackCtx
{
    void* user;
    Tcp& tcp;
    ConnId id;
    const TcpEvent& ev;
    const TcpSocketKey& key;
};

struct AcceptCallbackCtx
{
    void* user;
    Tcp& tcp;
    ListenId lid;
    Connection& newConn;
    const TcpSocketKey& key;
};

struct RecvCallbackCtx
{
    void* user;
    Tcp& tcp;
    ConnId id;
    RxConsumer& consumer;
    const TcpSocketKey& key;
};

using ConnCallback = void(*)(ConnCallbackCtx&) noexcept;
using AcceptCallback = void(*)(AcceptCallbackCtx&) noexcept;
using RecvCallback = void(*)(RecvCallbackCtx&) noexcept;

struct PoolConfig final
{
    size_t blockSize = 4096;
    size_t slabBlocks = 128;
    size_t maxBlocks = 0;
};

struct Config final
{
    Config() {};
    TcpPort ephemeralMin{49152};
    TcpPort ephemeralMax{65535};
    size_t maxConnections{4096};

    PoolConfig poolConfigs;

    TcpSocketPolicy defaults{};
};

struct ListenOptions final
{
    ListenOptions() {}
    TcpSocketPolicy policy{};
    TcpInterfaceBind bind{};
    size_t backlog{128};

    AcceptCallback onAccept{nullptr};
    void* onAcceptUser{nullptr};

    ConnCallback acceptConnCallback{nullptr};
    void* acceptedConnUser{nullptr};

    RecvCallback recvCallback{nullptr};
    void* recvUser{nullptr};
};

struct ConnectOptions final
{
    ConnectOptions() {}
    TcpSocketPolicy policy{};
    TcpInterfaceBind bind{};

    ConnCallback callback{nullptr};
    void* callbackUser{nullptr};

    RecvCallback recvCallback{nullptr};
    void* recvUser{nullptr};
};

struct TcpIpAdapter final
{
    static int af(const types::IPAddress& ip) noexcept
    {
        if (ip.isIPv6())
            return AF_INET6;
        else
            return AF_INET;
    }

    static bool unspecified(const types::IPAddress& ip) noexcept
    {
        return ip.isUnspecified();
    }

    static void writeSocketaddr(const types::IPAddress& ipIn, TcpPort portIn, void* sockaddrOut, uint32_t* socklenOut) noexcept
    {
        auto* ss = reinterpret_cast<sockaddr_storage*>(sockaddrOut);
        std::memset(ss, 0, sizeof(*ss));

        if (af(ipIn) == AF_INET)
        {
            sockaddr_in sin{};
            sin.sin_family = AF_INET;
            sin.sin_port = htons(portIn);

            sin.sin_addr.s_addr = ipIn.v4(); // v4() returns network-byte-order uint32_t

            std::memcpy(ss, &sin, sizeof(sin));
            *socklenOut = sizeof(sockaddr_in);
        }
        else
        {
            sockaddr_in6 sin6{};
            sin6.sin6_family = AF_INET6;
            sin6.sin6_port = htons(portIn);

        {
            auto it = ipIn.v6raw().begin();
            uint8_t* dst = reinterpret_cast<uint8_t*>(&sin6.sin6_addr);
            for (size_t i = 0; i < 16; ++i, ++it)
                dst[i] = *it;
        }

            std::memcpy(ss, &sin6, sizeof(sin6));
            *socklenOut = sizeof(sockaddr_in6);
        }
    }

    static void readSockaddr(const void* sockaddrIn, uint32_t socklenIn, types::IPAddress& ipOut, TcpPort& portOut) noexcept
    {
        (void)socklenIn;

        const auto* sa = reinterpret_cast<const sockaddr*>(sockaddrIn);

        if (sa->sa_family == AF_INET)
        {
            const auto* sin = reinterpret_cast<const sockaddr_in*>(sa);
            portOut = ntohs(sin->sin_port);

            ipOut.setV4(utils::readU32(reinterpret_cast<const uint8_t*>(&sin->sin_addr)));
            return;
        }

        if (sa->sa_family == AF_INET6)
        {
            const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(sa);
            portOut = ntohs(sin6->sin6_port);

            ipOut.setV6(utils::readU128(reinterpret_cast<const uint8_t*>(&sin6->sin6_addr)));
            return;
        }
    }
};
}

} // namespace transport

namespace std
{
template <>
struct hash<transport::tcp::TcpEndpoint>
{
    size_t operator()(const transport::tcp::TcpEndpoint& k) const noexcept
    {
        size_t h = std::hash<types::IPAddress>{}(k.address);
        size_t p = static_cast<size_t>(k.port);

        p ^= h + 0x9e3779b7f4a7c15ull + (p << 6) + (p >> 2);
        return p;
    }
};

template <>
struct hash<transport::tcp::TcpSocketKey>
{
    size_t operator()(const transport::tcp::TcpSocketKey& k) const noexcept
    {
        size_t h1 = std::hash<transport::tcp::TcpEndpoint>{}(k.local);
        size_t h2 = std::hash<transport::tcp::TcpEndpoint>{}(k.remote);

        h2 ^= h1 + 0x9e3779b7f4a7c15ull + (h2 << 6) + (h2 >> 2);
        return h2;
    }
};
}

#endif // TCP_TYPES_HPP

