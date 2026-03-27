/**
 * @file TcpTypes.hpp
 * @brief Shared type definitions for the TCP transport layer: endpoints, errors, events, and options.
 */

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

/**
 * @namespace transport::tcp
 * @brief Full per-VRF TCP stack: sockets, event dispatch, zero-copy TX, streaming RX.
 *
 * The primary entry point is @ref Tcp. Protocol code (e.g. BGP) obtains
 * @ref Listener and @ref Connection handles from it. All OS-level socket work
 * is delegated to @ref TcpEngine, which is never touched by callers directly.
 */
namespace tcp
{
class Connection;
class RxConsumer;
class Tcp;

using TcpPort  = uint16_t; ///< Host-byte-order TCP port number.
using ConnId   = uint64_t; ///< Opaque monotonically increasing connection identifier.
using ListenId = uint64_t; ///< Opaque monotonically increasing listener identifier.

// TCP CONNECTION STATES

/**
 * @brief RFC 793 TCP connection state machine states.
 * @ingroup TCP
 */
enum class TcpState : uint8_t
{
    CLOSED,       ///< No connection exists.
    LISTEN,       ///< Passive open; waiting for a SYN.
    SYN_SENT,     ///< Active open; SYN sent, waiting for SYN-ACK.
    SYN_RECEIVED, ///< SYN received and replied; waiting for ACK.
    ESTABLISHED,  ///< Full-duplex data transfer in progress.
    FIN_WAIT_1,   ///< Local side initiated close; FIN sent.
    FIN_WAIT_2,   ///< Remote ACKed local FIN; waiting for remote FIN.
    CLOSE_WAIT,   ///< Remote FIN received; waiting for local close.
    CLOSING,      ///< Both sides initiated simultaneous close.
    LAST_ACK,     ///< Passive close; FIN sent, waiting for final ACK.
    TIME_ACK,     ///< Awaiting ACK of the last FIN (non-standard alias).
    TIME_WAIT     ///< Waiting for stale packets to expire before final close.
};

/**
 * @brief Specifies which direction(s) of a connection to shut down.
 * @ingroup TCP
 */
enum class TcpShutdown : uint8_t
{
    READ,       ///< Discard all future inbound data.
    WRITE,      ///< Send FIN; no more data will be written.
    READ_WRITE  ///< Shut down both directions simultaneously.
};

// ERROR CODES

/**
 * @brief Transport-layer error codes returned by TCP operations.
 * @ingroup TCP
 *
 * These map loosely to POSIX errno values but are intentionally decoupled
 * from OS specifics so that upper layers do not need to interpret raw errno.
 */
enum class TcpErrc : uint8_t
{
    SUCCESS = 0,

    INVALID_ARGUMENT,   ///< Caller supplied a malformed argument (e.g. bad address family).
    NOT_FOUND,          ///< The requested ConnId or ListenId does not exist.
    NO_RESOURCES,       ///< Pool exhausted or OS refused the allocation.
    NOT_SUPPORTED,      ///< Operation not supported on this socket type.
    PERMISSION,         ///< Insufficient privileges (e.g. binding a reserved port).

    WOULD_BLOCK,        ///< Non-blocking operation would have blocked; retry later.
    IN_PROGRESS,        ///< Non-blocking connect is still in progress.

    TIMED_OUT,          ///< Connection or operation timed out.
    CONNECTION_REFUSED, ///< Remote actively refused the connection.
    CONNECTION_RESET,   ///< Connection was reset by the peer.
    NOT_CONNECTED,      ///< Operation requires an established connection.
    BROKEN_PIPE,        ///< Write on a connection whose read end has been closed.

    ADDRESS_IN_USE,       ///< Bind failed because the local address/port is already bound.
    ADDRESS_NOT_AVAILABLE, ///< The requested local address does not exist on any interface.

    NETWORK_UNREACHABLE, ///< No route to the destination network.
    HOST_UNREACHABLE,    ///< No route to the destination host.

    SYSTEM_ERROR ///< Unclassified OS error; inspect the accompanying osErrno field.
};

/**
 * @brief Combines a @ref TcpErrc classification with the raw OS errno.
 * @ingroup TCP
 */
struct TcpError final
{
    TcpErrc code{TcpErrc::SUCCESS};
    int osErrno{0}; ///< Raw errno value from the failing syscall; 0 if not applicable.

    constexpr bool ok() const noexcept { return code == TcpErrc::SUCCESS; }
};

// ENDPOINTS & SOCKET KEY

/**
 * @brief An IP address and port pair identifying one end of a TCP connection.
 * @ingroup TCP
 */
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

/**
 * @brief The 4-tuple (local endpoint, remote endpoint) that uniquely identifies a TCP socket.
 * @ingroup TCP
 *
 * Used as a lookup key in @ref TcpEngine's connection table and passed to all
 * event callbacks so callers can correlate events with their own state.
 */
struct TcpSocketKey final
{
    TcpEndpoint local{};
    TcpEndpoint remote{};

    /**
     * @brief Returns a copy of this key with local and remote endpoints swapped.
     *
     * Useful when comparing keys from both sides of a connection (e.g. matching
     * an outgoing SYN against an incoming SYN-ACK).
     */
    TcpSocketKey flipped() const noexcept { return TcpSocketKey{remote, local}; }

    /**
     * @brief Returns true if this socket key belongs to a connection accepted by the given listener endpoint.
     *
     * Matches on port first; if @p listenLocal carries an unspecified address
     * (wildcard bind), only the port is compared.
     *
     * @param listenLocal The local endpoint that the listener was bound to.
     */
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

// SOCKET POLICY & BIND OPTIONS

/**
 * @brief Per-socket quality-of-service and behavioral options.
 * @ingroup TCP
 *
 * Applied once at socket creation time. Fields left as `std::nullopt` are not
 * explicitly set, inheriting the OS default.
 */
struct TcpSocketPolicy final
{
    std::optional<uint8_t> ttl{};  ///< IP TTL (IPv4) or hop limit (IPv6) override.
    std::optional<uint8_t> tos{};  ///< DSCP/ECN byte (IPv4 TOS / IPv6 TCLASS) override.
    bool lowLatency{false};        ///< If true, sets TCP_NODELAY to disable Nagle's algorithm.
    bool keepAlive{false};         ///< If true, enables SO_KEEPALIVE for idle connection detection.
    bool pathMtuDiscovery{false};  ///< If true, sets the DF bit (IP_MTU_DISCOVER) to probe PMTU.
};

/**
 * @brief Optional interface binding constraint for a socket.
 * @ingroup TCP
 *
 * When @p ifname is non-null the socket is bound to the named interface via
 * SO_BINDTODEVICE, restricting traffic to that interface regardless of the
 * routing table.
 */
struct TcpInterfaceBind final
{
    const char* ifname{nullptr}; ///< Interface name string (e.g. "eth0"); not required to be null-terminated if ifnameLen is set.
    uint32_t ifnameLen{0};       ///< Length of @p ifname in bytes, excluding any null terminator.
};

// EVENTS

/**
 * @brief Discriminator for events delivered through @ref TcpEvent and the callback API.
 * @ingroup TCP
 */
enum class TcpEventType : uint8_t
{
    ACCEPT_READY, ///< A listener has one or more pending connections ready to accept.
    ACCEPTED,     ///< A new server-side connection has been accepted; @ref AcceptCallbackCtx is populated.
    CONNECTED,    ///< An outbound non-blocking connect() has completed successfully.
    READABLE,     ///< The socket has inbound data available; recv() will not block.
    WRITABLE,     ///< The socket send buffer has space; send() will not block.
    PEER_CLOSED,  ///< The remote performed an orderly shutdown (received FIN with 0 bytes).
    ERROR         ///< The connection has encountered an error; inspect @ref TcpEvent::error.
};

/**
 * @brief Carries event type, connection identity, and any associated error in the pull-mode API.
 * @ingroup TCP
 *
 * Used by @ref TcpEngine::pollEvents when callers prefer to drain events into
 * a span rather than registering push callbacks.
 */
struct TcpEvent final
{
    TcpEventType type{};
    uint64_t id{0};    ///< @ref ConnId or @ref ListenId the event applies to.
    TcpError error{};
};

// CALLBACK CONTEXT TYPES

/**
 * @brief Context delivered to a @ref ConnCallback for connection-level events.
 * @ingroup TCP
 *
 * Passed by reference; the callback must not store the context beyond its
 * own call frame.
 */
struct ConnCallbackCtx
{
    void* user;           ///< Opaque pointer registered with the connection options.
    Tcp& tcp;             ///< Owning TCP stack.
    ConnId id;            ///< Connection that generated the event.
    const TcpEvent& ev;   ///< The event (type, error).
    const TcpSocketKey& key; ///< 4-tuple of the connection at event time.
};

/**
 * @brief Context delivered to an @ref AcceptCallback when a new connection is accepted.
 * @ingroup TCP
 *
 * Passed by reference; the callback must not store the context beyond its
 * own call frame. The @ref Connection handle in @p newConn is valid only for
 * the duration of the callback unless the caller moves it into its own storage.
 */
struct AcceptCallbackCtx
{
    void* user;          ///< Opaque pointer registered with the listener options.
    Tcp& tcp;            ///< Owning TCP stack.
    ListenId lid;        ///< Listener that accepted the connection.
    Connection& newConn; ///< The newly accepted connection handle.
    const TcpSocketKey& key; ///< 4-tuple of the newly accepted connection.
};

/**
 * @brief Context delivered to a @ref RecvCallback when inbound data is available.
 * @ingroup TCP
 *
 * The @ref RxConsumer provides a zero-copy view into the receive buffer.
 * Calling @ref RxConsumer::commit() advances the read pointer; data not
 * committed will be re-presented on the next callback invocation.
 */
struct RecvCallbackCtx
{
    void* user;              ///< Opaque pointer registered with the connection or listener options.
    Tcp& tcp;                ///< Owning TCP stack.
    ConnId id;               ///< Connection that has data available.
    RxConsumer& consumer;    ///< Zero-copy view into the receive buffer.
    const TcpSocketKey& key; ///< 4-tuple of the connection.
};

// CALLBACK FUNCTION POINTER TYPES

using ConnCallback   = void(*)(ConnCallbackCtx&)   noexcept; ///< Connection-level event callback.
using AcceptCallback = void(*)(AcceptCallbackCtx&) noexcept; ///< New accepted-connection callback.
using RecvCallback   = void(*)(RecvCallbackCtx&)   noexcept; ///< Inbound-data callback.

// CONFIGURATION

/**
 * @brief Configuration for the shared @ref TxBufferPool used by all connections.
 * @ingroup TCP
 */
struct PoolConfig final
{
    size_t blockSize = 4096;   ///< Size in bytes of each individual buffer block.
    size_t slabBlocks = 128;   ///< Number of blocks allocated per slab growth.
    size_t maxBlocks = 0;      ///< Maximum total blocks the pool may hold; 0 = unlimited.
};

/**
 * @brief Top-level configuration for a @ref Tcp / @ref TcpEngine instance.
 * @ingroup TCP
 */
struct Config final
{
    Config() {};
    TcpPort ephemeralMin{49152};  ///< Lower bound of the ephemeral port range for active connects.
    TcpPort ephemeralMax{65535};  ///< Upper bound of the ephemeral port range.
    size_t maxConnections{4096};  ///< Hard cap on the number of simultaneous connections.

    PoolConfig poolConfigs; ///< TX buffer pool sizing parameters.

    TcpSocketPolicy defaults{}; ///< Default socket policy applied to all connections unless overridden.
};

/**
 * @brief Options controlling the creation and callback wiring of a passive listener.
 * @ingroup TCP
 *
 * Passed to @ref Tcp::listen(). The two callback pairs (`onAccept` /
 * `acceptConnCallback` and `recvCallback`) serve different purposes:
 * `onAccept` is invoked once per accepted socket to let the caller inspect or
 * reject it; `recvCallback` is invoked each time data arrives on any accepted
 * connection owned by this listener.
 */
struct ListenOptions final
{
    ListenOptions() {}
    TcpSocketPolicy policy{};
    TcpInterfaceBind bind{};
    size_t backlog{128}; ///< Depth of the OS listen() backlog queue.

    AcceptCallback onAccept{nullptr};   ///< Called once per newly accepted connection.
    void* onAcceptUser{nullptr};        ///< Opaque user pointer passed to @ref onAccept.

    ConnCallback acceptConnCallback{nullptr}; ///< Per-connection event callback for accepted sockets.
    void* acceptedConnUser{nullptr};          ///< Opaque user pointer passed to @ref acceptConnCallback.

    RecvCallback recvCallback{nullptr}; ///< Called when data arrives on any accepted connection.
    void* recvUser{nullptr};            ///< Opaque user pointer passed to @ref recvCallback.
};

/**
 * @brief Options controlling the creation and callback wiring of an active outbound connection.
 * @ingroup TCP
 */
struct ConnectOptions final
{
    ConnectOptions() {}
    TcpSocketPolicy policy{};
    TcpInterfaceBind bind{};

    ConnCallback callback{nullptr};   ///< Called on connect completion, errors, and close.
    void* callbackUser{nullptr};      ///< Opaque user pointer passed to @ref callback.

    RecvCallback recvCallback{nullptr}; ///< Called when inbound data is available.
    void* recvUser{nullptr};            ///< Opaque user pointer passed to @ref recvCallback.
};

// ADDRESS ADAPTER

/**
 * @brief Bridges the project's @ref types::IPAddress to POSIX sockaddr structures.
 * @ingroup TCP
 *
 * Used exclusively by @ref TcpEngine to convert between the internal IP address
 * representation and the `sockaddr_in` / `sockaddr_in6` layout required by the
 * socket API. Methods are intentionally static with no state.
 */
struct TcpIpAdapter final
{
    /**
     * @brief Returns `AF_INET` or `AF_INET6` for the given address.
     *
     * @param ip Source IP address.
     * @return POSIX address family constant.
     */
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

    /**
     * @brief Serialises an @ref types::IPAddress and port into a POSIX sockaddr buffer.
     *
     * Writes either a `sockaddr_in` or `sockaddr_in6` into `*sockaddrOut`,
     * and sets `*socklenOut` to the actual struct size.
     *
     * @param ipIn        Source IP address.
     * @param portIn      Port in host byte order.
     * @param sockaddrOut Must point to a buffer of at least `sizeof(sockaddr_storage)`.
     * @param socklenOut  Receives the size of the written sockaddr struct.
     */
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

    /**
     * @brief Deserialises a POSIX sockaddr buffer into an @ref types::IPAddress and port.
     *
     * Supports `AF_INET` and `AF_INET6`; silently does nothing for unknown families.
     *
     * @param sockaddrIn  Pointer to a valid `sockaddr_in` or `sockaddr_in6` buffer.
     * @param socklenIn   Length of the buffer (currently unused but kept for API symmetry).
     * @param ipOut       Receives the parsed IP address.
     * @param portOut     Receives the parsed port in host byte order.
     */
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
