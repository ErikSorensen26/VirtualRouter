// TcpEngine.cpp

#include "TcpEngine.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdexcept>

#include <algorithm>

#include "Listener.h"
#include "Connection.h"
#include "tcp/rx/RxConsumer.h"

namespace transport::tcp
{
static TcpError mapErrno(int e) noexcept
{
    TcpError out{};
    out.osErrno = e;

    switch (e)
    {
        case 0: out.code = TcpErrc::SUCCESS; break;

        case EAGAIN: out.code = TcpErrc::WOULD_BLOCK; break;

        case EINPROGRESS:
        case EALREADY: out.code = TcpErrc::IN_PROGRESS; break;

        case ETIMEDOUT: out.code = TcpErrc::TIMED_OUT; break;
        case ECONNREFUSED: out.code = TcpErrc::CONNECTION_REFUSED; break;
        case ECONNRESET: out.code = TcpErrc::CONNECTION_RESET; break;
        case ENOTCONN: out.code = TcpErrc::NOT_CONNECTED; break;
        case EPIPE: out.code = TcpErrc::BROKEN_PIPE; break;
        case EADDRINUSE: out.code = TcpErrc::ADDRESS_IN_USE; break;
        case EADDRNOTAVAIL: out.code = TcpErrc::ADDRESS_NOT_AVAILABLE; break;
        case ENETUNREACH: out.code = TcpErrc::NETWORK_UNREACHABLE; break;
        case EHOSTUNREACH: out.code = TcpErrc::HOST_UNREACHABLE; break;
        case EPERM:
        case EACCES: out.code = TcpErrc::PERMISSION; break;
        case ENOMEM:
        case ENOBUFS: out.code = TcpErrc::NO_RESOURCES; break;
        default: out.code = TcpErrc::SYSTEM_ERROR; break;
    }

    return out;
}

static bool shouldStickify(const TcpError& e) noexcept
{
    return e.code != TcpErrc::SUCCESS &&
           e.code != TcpErrc::WOULD_BLOCK &&
           e.code != TcpErrc::IN_PROGRESS;
}

static int setNonBlocking(int fd) noexcept
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

static void applyPolicy(int fd, int af, const TcpSocketPolicy& p)
{
    if (p.lowLatency)
    {
        int one = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0)
            throw std::runtime_error("Tcp Policy Error");
    }

    if (p.keepAlive)
    {
        int one = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one)) != 0)
            throw std::runtime_error("Tcp Policy Error");
    }

    if (p.tos.has_value())
    {
        int v = static_cast<int>(*p.tos);
        if (af == AF_INET)
        {
            if (setsockopt(fd, IPPROTO_IP, IP_TOS, &v, sizeof(v)) != 0)
                throw std::runtime_error("Tcp Socket Error");
        }
        else if (af == AF_INET6)
        {
            if (setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &v, sizeof(v)) != 0)
                throw std::runtime_error("Tcp Socket Error");
        }
        else throw std::runtime_error("Tcp Invalid Arguments");
    }

    if (p.ttl.has_value())
    {
        int v = static_cast<int>(*p.ttl);
        if (af == AF_INET)
        {
            if (setsockopt(fd, IPPROTO_IP, IP_TTL, &v, sizeof(v)) != 0)
                throw std::runtime_error("Tcp Socket Error");
        }
        else if (af == AF_INET6)
        {
            if (setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &v, sizeof(v)) != 0)
                throw std::runtime_error("Tcp Socket Error");
        }
        else throw std::runtime_error("Tcp Invalid Arguments");
    }

    if (af == AF_INET)
    {
        int v = p.pathMtuDiscovery ? IP_PMTUDISC_DO : IP_PMTUDISC_DONT;
        if (setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &v, sizeof(v)) != 0)
            throw std::runtime_error("Tcp Socket Error");
    }
    else if (af == AF_INET6)
    {
        int v = p.pathMtuDiscovery ? IPV6_PMTUDISC_DO : IPV6_PMTUDISC_DONT;
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &v, sizeof(v)) != 0)
            throw std::runtime_error("Tcp Socket Error");
    }
}

static TcpSocketPolicy mergePolicy(const TcpSocketPolicy& defaults, const TcpSocketPolicy& overrides) noexcept
{
    TcpSocketPolicy out = defaults;
    if (overrides.ttl.has_value()) out.ttl = overrides.ttl;
    if (overrides.tos.has_value()) out.tos = overrides.tos;
    out.lowLatency        = overrides.lowLatency        || defaults.lowLatency;
    out.keepAlive         = overrides.keepAlive         || defaults.keepAlive;
    out.pathMtuDiscovery  = overrides.pathMtuDiscovery  || defaults.pathMtuDiscovery;
    return out;
}

static TcpState mapLinuxTcpState(uint8_t s) noexcept
{
    switch (s)
    {
        case 1:  return TcpState::ESTABLISHED;
        case 2:  return TcpState::SYN_SENT;
        case 3:  return TcpState::SYN_RECEIVED;
        case 4:  return TcpState::FIN_WAIT_1;
        case 5:  return TcpState::FIN_WAIT_2;
        case 6:  return TcpState::TIME_WAIT;
        case 7:  return TcpState::CLOSED;
        case 8:  return TcpState::CLOSE_WAIT;
        case 9:  return TcpState::LAST_ACK;
        case 10: return TcpState::LISTEN;
        case 11: return TcpState::CLOSING;
        default: return TcpState::CLOSED;
    }
}

TcpEngine::TcpEngine(core::VirtualRouter& v, const Config& c)
    : vr(v), cfg(c), bufferPool(cfg.poolConfigs)
{
    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) epfd = -1;

    connections.reserve(cfg.maxConnections);
    connections.max_load_factor(0.70f);

    epScratch.reserve(256);
    ioScratch.resize(64 * 1024);
}

TcpEngine::~TcpEngine()
{
    // Close listeners (also closes accepted); drain by key since closeListener erases from the map
    while (!listeners.empty())
        closeListener(listeners.begin()->first);

    // Close any remaining (outbound)
    while (!connections.empty())
        closeConnectionInternal(connections.begin()->first);

    if (epfd >= 0) ::close(epfd);
    epfd = -1;
}

TcpPort TcpEngine::allocateEphemeral() noexcept
{
    if (cfg.ephemeralMin > cfg.ephemeralMax) return 0;
    if (nextEphemeral < cfg.ephemeralMin || nextEphemeral > cfg.ephemeralMax)
        nextEphemeral = cfg.ephemeralMin;

    TcpPort p = nextEphemeral++;
    if (nextEphemeral > cfg.ephemeralMax) nextEphemeral = cfg.ephemeralMin;
    return p;
}

void TcpEngine::epAdd(int fd, uint64_t tag, uint32_t events) noexcept
{
    if (epfd < 0) return;
    epoll_event ev{};
    ev.events = events;
    ev.data.u64 = tag;
    (void)epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

void TcpEngine::epDel(int fd) noexcept
{
    if (epfd < 0) return;
    (void)epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}

void TcpEngine::bindToDeviceIfRequested(int fd, const TcpInterfaceBind& b)
{
    if (!b.ifname || b.ifnameLen == 0) return;

    if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, b.ifname, static_cast<socklen_t>(b.ifnameLen)) != 0)
        throw std::runtime_error("Tcp error while binding to device");
}

bool TcpEngine::bindEndpoint(int fd, const TcpEndpoint& ep) noexcept
{
    sockaddr_storage ss{};
    uint32_t slen = 0;
    TcpIpAdapter::writeSocketaddr(ep.address, ep.port, &ss, &slen);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&ss), static_cast<socklen_t>(slen)) != 0)
        return false;
    return true;
}

bool TcpEngine::getLiveKey(int fd, TcpSocketKey& out) const noexcept
{
    sockaddr_storage lss{};
    sockaddr_storage rss{};
    socklen_t llen = sizeof(lss);
    socklen_t rlen = sizeof(rss);

    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&lss), &llen) != 0)
        return false;

    types::IPAddress lip{};
    TcpPort lport{};
    TcpIpAdapter::readSockaddr(&lss, static_cast<uint32_t>(llen), lip, lport);
    out.local = TcpEndpoint{lip, lport};

    if (::getpeername(fd, reinterpret_cast<sockaddr*>(&rss), &rlen) != 0)
    {
        if (errno == ENOTCONN)
        {
            out.remote = TcpEndpoint{};
            return true;
        }
        return false;
    }

    types::IPAddress rip{};
    TcpPort rport{};
    TcpIpAdapter::readSockaddr(&rss, static_cast<uint32_t>(rlen), rip, rport);
    out.remote = TcpEndpoint{rip, rport};
    return true;
}

TcpState TcpEngine::linuxState(int fd) const
{
    tcp_info info{};
    socklen_t len = sizeof(info);
    if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &len) != 0)
        throw std::runtime_error("Error requesting linux state");

    return mapLinuxTcpState(info.tcpi_state);
}

TcpEngine::ConnectionState* TcpEngine::getConnection(ConnId cid) noexcept
{
    auto it = connections.find(cid);
    if (it == connections.end())
        return nullptr;
    return &it->second;
}

TcpEngine::ConnectionState* TcpEngine::getAcceptedConnectionChecked(ListenId lid, ConnId cid) noexcept
{
    auto c = getConnection(cid);
    if (!c) return c;
    if (c->ownerListener != lid)
        return nullptr;
    return c;
}

Listener TcpEngine::makeListenerHandle(ListenId id) noexcept
{
    return Listener(this, id);
}

Connection TcpEngine::makeConnectionHandle(ConnId cid, TxBuffer& bufTx) noexcept
{
    return Connection(this, cid, bufTx);
}

Listener TcpEngine::createListener(const TcpEndpoint& local, const ListenOptions& opt)
{
    const int af = TcpIpAdapter::af(local.address);
    if (af != AF_INET && af != AF_INET6)
        throw std::runtime_error("Tcp Invalid Arguments");

    int fd = ::socket(af, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) throw std::runtime_error("Error while creating socket");

    TcpSocketPolicy p{};
    try
    {
        if (setNonBlocking(fd) != 0) throw std::runtime_error("Tcp error while setting nonblock.");

        {
            int one = 1;
            (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        }

        bindToDeviceIfRequested(fd, opt.bind);

        p = mergePolicy(cfg.defaults, opt.policy);
        applyPolicy(fd, af, p);

        if (!bindEndpoint(fd, local))
            throw std::runtime_error("Tcp error while binding");

        if (::listen(fd, static_cast<int>(opt.backlog)) != 0)
            throw std::runtime_error("Tcp error while listening");
    }
    catch (...)
    {
        ::close(fd);
        throw;
    }

    ListenId id = nextListenId++;

    ListenerState lst{};
    lst.id = id;
    lst.fd = fd;
    lst.af = af;
    lst.local = local;
    lst.backlog = opt.backlog;
    lst.policyApplied = p;
    lst.rxSize = opt.rxBufferSize;

    lst.onAccept = opt.onAccept;
    lst.onAcceptUser = opt.onAcceptUser;
    lst.acceptedConnCallback = opt.acceptConnCallback;
    lst.acceptedConnUser = opt.acceptedConnUser;
    lst.recvCallback = opt.recvCallback;
    lst.recvUser = opt.recvUser;

    listeners.emplace(id, std::move(lst));

    if (epfd >= 0)
        epAdd(fd, packListener(id), EPOLLIN | EPOLLET);

    return Listener(this, id);
}

Connection TcpEngine::createConnection(const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opt)
{
    const int af = TcpIpAdapter::af(remote.address);
    if (af != AF_INET && af != AF_INET6)
        throw std::runtime_error("Tcp Invalid Arguments");

    if (connections.size() >= cfg.maxConnections)
        throw std::runtime_error("Tcp No resources");

    int fd = ::socket(af, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) throw std::runtime_error("Tcp error when connecting to socket");

    TcpEndpoint bindEp = local;
    bool pending = false;

    try
    {
        if (setNonBlocking(fd) != 0) throw std::runtime_error("Error when setting nonblocking");

        bindToDeviceIfRequested(fd, opt.bind);

        const TcpSocketPolicy p = mergePolicy(cfg.defaults, opt.policy);
        applyPolicy(fd, af, p);

        if (bindEp.port == 0)
        {
            if (cfg.ephemeralMin > cfg.ephemeralMax)
                throw std::runtime_error("Tcp out of ephemeral ports");

            const uint32_t range = static_cast<uint32_t>(cfg.ephemeralMax - cfg.ephemeralMin + 1);
            bool bound = false;

            for (uint32_t i = 0; i < range; ++i)
            {
                bindEp.port = allocateEphemeral();
                if (bindEndpoint(fd, bindEp))
                {
                    bound = true;
                    break;
                }
            }

            if (!bound)
                throw std::runtime_error("No ephemeral ports open");
        }
        else
        {
            if (!bindEndpoint(fd, bindEp))
                throw std::runtime_error("Tcp error while binding");
        }

        sockaddr_storage rss{};
        uint32_t rlen = 0;
        TcpIpAdapter::writeSocketaddr(remote.address, remote.port, &rss, &rlen);

        int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&rss), static_cast<socklen_t>(rlen));

        if (rc != 0)
        {
            if (errno != EINPROGRESS)
                throw std::runtime_error("Tcp error while connecting");
            pending = true;
        }
    }
    catch (...)
    {
        ::close(fd);
        throw;
    }

    ConnId cid = nextConnId++;

    auto cit = connections.try_emplace(cid, cid, bufferPool, opt.rxBufferSize);
    auto& c = cit.first->second;

    c.fd = fd;
    c.ownerListener = 0;
    c.connectPending = pending;

    c.cb = opt.callback;
    c.cbUser = opt.callbackUser;

    c.recvCb = opt.recvCallback;
    c.recvUser = opt.recvUser;

    c.key.local = bindEp;
    c.key.remote = remote;

    {
        TcpSocketKey live{};
        if (getLiveKey(fd, live))
            c.key.local = live.local;
    }

    if (epfd >= 0)
        epAdd(fd, packConn(cid), EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET);

    return Connection(this, cid, c.bufferTx);
}

ConnId TcpEngine::adoptAcceptedSocket(ListenerState& lst, int cfd) noexcept
{
    if (connections.size() >= cfg.maxConnections)
    {
        ::close(cfd);
        return 0;
    }

    try
    {
        applyPolicy(cfd, lst.af, lst.policyApplied);
    }
    catch (...)
    {
        ::close(cfd);
        return 0;
    }

    TcpSocketKey k{};
    if (!getLiveKey(cfd, k))
    {
        ::close(cfd);
        return 0;
    }

    ConnId cid = nextConnId++;

    ConnectionState c(cid, bufferPool, lst.rxSize);
    c.fd = cfd;
    c.ownerListener = lst.id;
    c.connectPending = false;

    c.cb = lst.acceptedConnCallback;
    c.cbUser = lst.acceptedConnUser;

    c.recvCb = lst.recvCallback;
    c.recvUser = lst.recvUser;
    c.key = k;

    connections.emplace(cid, std::move(c));
    lst.accepted.push_back(cid);

    if (epfd >= 0)
        epAdd(cfd, packConn(cid), EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET);

    return cid;
}

size_t TcpEngine::acceptLoop(ListenId lid, Tcp* tcp, std::span<TcpEvent> acceptEvents, size_t& produced, bool invokeCallbacks) noexcept
{
    size_t accepted = 0;

    while (true)
    {
        // re-find each iteration: a callback below may have closed the listener
        auto itl = listeners.find(lid);
        if (itl == listeners.end()) break;
        ListenerState& lst = itl->second;

        int cfd = ::accept4(lst.fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (cfd < 0)
        {
            if (errno == EINTR) continue;
            break;
        }

        auto cid = adoptAcceptedSocket(lst, cfd);
        if (cid == 0) continue;

        ++accepted;

        if (produced < acceptEvents.size())
            acceptEvents[produced++] = TcpEvent{ TcpEventType::ACCEPTED, cid, {} };

        if (invokeCallbacks && tcp)
        {
            const AcceptCallback onAccept = lst.onAccept;
            void* const onAcceptUser = lst.onAcceptUser;

            auto itc = connections.find(cid);
            if (itc != connections.end() && onAccept)
            {
                Connection conn{this, cid, itc->second.bufferTx};
                AcceptCallbackCtx ctx{onAcceptUser, *tcp, lid, conn, itc->second.key};
                onAccept(ctx);
                conn.release(); // if the callback didn't take the handle, the listener keeps ownership
            }

            itc = connections.find(cid); // the callback may have closed the connection
            if (itc != connections.end() && itc->second.cb)
            {
                TcpEvent ev{ TcpEventType::ACCEPTED, cid, {} };
                ConnCallbackCtx ctx{itc->second.cbUser, *tcp, cid, ev, itc->second.key};
                itc->second.cb(ctx);
            }
        }
    }

    return accepted;
}

void TcpEngine::dispatchConnectEvent(Tcp& tcp, ConnId cid, TcpEventType t, TcpError e) noexcept
{
    auto itc = connections.find(cid);
    if (itc == connections.end()) return;

    auto& c = itc->second;
    if (!c.cb) return;

    TcpEvent ev{};
    ev.type = t;
    ev.id = cid;
    ev.error = e;

    ConnCallbackCtx ctx{c.cbUser, tcp, cid, ev, c.key};
    c.cb(ctx);
}

size_t TcpEngine::flush(ConnId cid) noexcept
{
    auto c = getConnection(cid);
    if (!c) return 0;

    ConnectionState& cs = *c;
    if (!cs.stickyError.ok()) return 0;
    if (cs.fd < 0) return 0; // mock connection: no socket to send() on

    size_t totalSent = 0;

    while (!cs.bufferTx.empty())
    {
        auto span = cs.bufferTx.peek(0);
        if (span.empty()) break;

        ssize_t n = ::send(cs.fd, span.data(), span.size(), MSG_NOSIGNAL);

        if (n > 0)
        {
            size_t sent = static_cast<size_t>(n);
            cs.bufferTx.consume(sent);
            totalSent += sent;
            continue;
        }

        if (n == 0) break;

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;

        TcpError e = mapErrno(errno);
        if (shouldStickify(e))
            cs.stickyError = e;

        break;
    }

    return totalSent;
}

size_t TcpEngine::read(ConnId cid, std::span<uint8_t> out) noexcept
{
    auto c = getConnection(cid);
    if (!c || out.empty()) return 0;
    if (c->fd < 0) return 0; // mock connection: no socket to recv() from

    while (true)
    {
        ssize_t n = ::recv(c->fd, out.data(), out.size(), 0);
        if (n > 0) return static_cast<size_t>(n);

        if (n == 0)
        {
            c->peerClosed = true;
            return 0;
        }

        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;

        TcpError e = mapErrno(errno);
        if (shouldStickify(e)) c->stickyError = e;
        return 0;
    }
}

void TcpEngine::shutdownConnection(ConnId cid, TcpShutdown how) noexcept
{
    auto c = getConnection(cid);
    if (!c) return;
    if (c->fd < 0) return; // mock connection: no socket to shut down

    if (how != TcpShutdown::READ)
        (void)flush(cid); // drain buffered TX before the FIN

    const int flag = (how == TcpShutdown::READ)  ? SHUT_RD
                   : (how == TcpShutdown::WRITE) ? SHUT_WR
                                                 : SHUT_RDWR;
    (void)::shutdown(c->fd, flag);
}

TcpState TcpEngine::connectionState(ConnId cid) const noexcept
{
    auto it = connections.find(cid);
    if (it == connections.end()) return TcpState::CLOSED;
    if (it->second.fd < 0) return TcpState::ESTABLISHED; // mock connection: no socket to query

    tcp_info info{};
    socklen_t len = sizeof(info);
    if (getsockopt(it->second.fd, IPPROTO_TCP, TCP_INFO, &info, &len) != 0)
        return TcpState::CLOSED;

    return mapLinuxTcpState(info.tcpi_state);
}

void TcpEngine::listenerDisconnect(ListenId lid, ConnId cid) noexcept
{
    auto itl = listeners.find(lid);
    if (itl == listeners.end())
        return;

    auto c = getAcceptedConnectionChecked(lid, cid);
    if (!c) return;

    closeConnectionInternal(cid);
}

void TcpEngine::dropLocalConnections(const types::IPAddress& addr) noexcept
{
    std::vector<ConnId> toClose;
    for (const auto& [cid, c] : connections)
    {
        if (c.key.local.address == addr)
            toClose.push_back(cid);
    }
    for (ConnId cid : toClose)
        closeConnectionInternal(cid);
}

void TcpEngine::closeConnectionInternal(ConnId cid) noexcept
{
    // a recv callback closing its own connection: defer until its RxConsumer is destroyed
    if (cid != 0 && cid == inCallbackCid)
    {
        deferredClose = true;
        return;
    }

    auto it = connections.find(cid);
    if (it == connections.end()) return;

    ConnectionState c = std::move(it->second);
    connections.erase(it);

    if (epfd >= 0 && c.fd >= 0) epDel(c.fd);

    if (c.fd >= 0)
    {
        (void)::shutdown(c.fd, SHUT_RDWR);
        ::close(c.fd);
    }

    if (c.ownerListener != 0)
    {
        auto itl = listeners.find(c.ownerListener);
        if (itl != listeners.end())
        {
            auto& v = itl->second.accepted;
            v.erase(std::remove(v.begin(), v.end(), cid), v.end());
        }
    }
}

void TcpEngine::closeConnection(ConnId cid) noexcept
{
    closeConnectionInternal(cid);
}

void TcpEngine::closeListener(ListenId lid) noexcept
{
    auto it = listeners.find(lid);
    if (it == listeners.end()) return;

    ListenerState lst = std::move(it->second);
    listeners.erase(it);

    if (epfd >= 0 && lst.fd >= 0) epDel(lst.fd);

    if (lst.fd >= 0) ::close(lst.fd);

    // Close all accepted connections owned by this listener
    auto acceptedCopy = lst.accepted;
    for (ConnId cid : acceptedCopy)
        closeConnectionInternal(cid);
}

std::optional<TcpSocketKey> TcpEngine::connectionSocketKey(ConnId cid) const noexcept
{
    auto it = connections.find(cid);
    if (it == connections.end())
        return std::nullopt;

    if (it->second.fd < 0) return it->second.key; // mock connection: no live socket to query, use cached key

    TcpSocketKey live{};
    if (!getLiveKey(it->second.fd, live))
        return std::nullopt;

    return live;
}

size_t TcpEngine::pollEvents(std::span<TcpEvent> outEvents, uint32_t timeoutMs) noexcept
{
    if (epfd < 0)
        return 0;

    if (outEvents.empty())
        return 0;

    epScratch.resize(outEvents.size());

    int n = ::epoll_wait(epfd, epScratch.data(), static_cast<int>(epScratch.size()),
                         static_cast<int>(timeoutMs));
    if (n < 0) return 0;

    size_t produced = 0;

    for (int i = 0; i < n && produced < outEvents.size(); ++i)
    {
        const uint32_t e = epScratch[i].events;
        const uint64_t tag = epScratch[i].data.u64;

        if (isListenerTag(tag))
        {
            ListenId lid = static_cast<ListenId>(unpackId(tag));

            // Accept by default (no callbacks here).
            acceptLoop(lid, nullptr, outEvents, produced, false);
            continue;
        }

        ConnId cid = static_cast<ConnId>(unpackId(tag));
        auto itc = connections.find(cid);
        if (itc == connections.end()) continue;

        auto& c = itc->second;

        if (c.connectPending && (e & (EPOLLOUT | EPOLLERR | EPOLLHUP)) && produced < outEvents.size())
        {
            int err = 0;
            socklen_t elen = sizeof(err);
            if (getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0)
            {
                if (err == 0)
                {
                    c.connectPending = false;
                    outEvents[produced++] = TcpEvent{ TcpEventType::CONNECTED, c.id, {} };
                }
                else
                {
                    c.stickyError = mapErrno(err);
                    outEvents[produced++] = TcpEvent{ TcpEventType::ERROR, c.id, c.stickyError };
                    continue;
                }
            }
            else
            {
                c.stickyError = mapErrno(errno);
                outEvents[produced++] = TcpEvent{ TcpEventType::ERROR, c.id, c.stickyError };
                continue;
            }
        }

        if ((e & EPOLLIN) && produced < outEvents.size())
            outEvents[produced++] = TcpEvent{ TcpEventType::READABLE, c.id, {} };

        if ((e & EPOLLOUT) && produced < outEvents.size())
            outEvents[produced++] = TcpEvent{ TcpEventType::WRITABLE, c.id, {} };

        if ((e & EPOLLRDHUP) && produced < outEvents.size())
            outEvents[produced++] = TcpEvent{ TcpEventType::PEER_CLOSED, c.id, {} };

        if ((e & EPOLLERR) && produced < outEvents.size())
        {
            int err = 0;
            socklen_t elen = sizeof(err);
            if (getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err != 0)
                c.stickyError = mapErrno(err);
            else
                c.stickyError = TcpError{TcpErrc::SYSTEM_ERROR, 0};

            outEvents[produced++] = TcpEvent{ TcpEventType::ERROR, c.id, c.stickyError };
        }
    }

    return produced;
}

size_t TcpEngine::pump(Tcp& tcp, uint32_t timeoutMs, size_t maxEvents) noexcept
{
    if (epfd < 0)
        return 0;

    if (maxEvents == 0)
        return 0;

    epScratch.resize(maxEvents);

    int n = ::epoll_wait(epfd, epScratch.data(), static_cast<int>(epScratch.size()),
                         static_cast<int>(timeoutMs));
    if (n < 0) return 0;

    size_t dispatched = 0;

    for (int i = 0; i < n; ++i)
    {
        const uint32_t e = epScratch[i].events;
        const uint64_t tag = epScratch[i].data.u64;

        if (isListenerTag(tag))
        {
            ListenId lid = static_cast<ListenId>(unpackId(tag));

            // Accept by default + invoke callbacks.
            size_t dummyProduced = 0;
            std::span<TcpEvent> noEvents{};
            dispatched += acceptLoop(lid, &tcp, noEvents, dummyProduced, true);
            continue;
        }

        ConnId cid = static_cast<ConnId>(unpackId(tag));
        ConnectionState* c = getConnection(cid);
        if (!c) continue;

        if (c->connectPending && (e & (EPOLLOUT | EPOLLERR | EPOLLHUP)))
        {
            int err = 0;
            socklen_t elen = sizeof(err);
            if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err == 0)
            {
                c->connectPending = false;
                dispatchConnectEvent(tcp, cid, TcpEventType::CONNECTED, {});
                ++dispatched;
            }
            else
            {
                c->stickyError = mapErrno(err != 0 ? err : errno);
                dispatchConnectEvent(tcp, cid, TcpEventType::ERROR, c->stickyError);
                ++dispatched;
                continue;
            }

            c = getConnection(cid); // the callback may have closed it
            if (!c) continue;
        }

        if (e & EPOLLIN)
        {
            // If recvCb is set, we deliver bytes and DO NOT require public recv().
            if (c->recvCb)
            {
                while (true)
                {
                    ssize_t rn = ::recv(c->fd, ioScratch.data(), ioScratch.size(), 0);
                    if (rn < 0)
                    {
                        if (errno == EINTR) continue;
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;

                        TcpError er = mapErrno(errno);
                        if (shouldStickify(er)) c->stickyError = er;
                        dispatchConnectEvent(tcp, cid, TcpEventType::ERROR, er);
                        ++dispatched;
                        break;
                    }

                    if (rn == 0)
                    {
                        c->peerClosed = true;
                        dispatchConnectEvent(tcp, cid, TcpEventType::PEER_CLOSED, {});
                        ++dispatched;
                        break;
                    }

                    // closes from inside the callback are deferred so the RxConsumer commits into live state
                    inCallbackCid = cid;
                    deferredClose = false;
                    {
                        RxConsumer consumer = c->bufferRx.consume(
                            std::span<uint8_t>(ioScratch.data(), static_cast<size_t>(rn)));
                        RecvCallbackCtx ctx{c->recvUser, tcp, cid, consumer, c->key};
                        c->recvCb(ctx);
                    }
                    inCallbackCid = 0;
                    ++dispatched;

                    if (deferredClose)
                    {
                        deferredClose = false;
                        closeConnectionInternal(cid);
                        break;
                    }

                    c = getConnection(cid);
                    if (!c) break;
                }
            }
            else
            {
                dispatchConnectEvent(tcp, cid, TcpEventType::READABLE, {});
                ++dispatched;
            }

            c = getConnection(cid); // callbacks above may have closed it
            if (!c) continue;
        }

        if (e & EPOLLOUT)
        {
            dispatchConnectEvent(tcp, cid, TcpEventType::WRITABLE, {});
            ++dispatched;

            c = getConnection(cid);
            if (!c) continue;
        }

        if (e & EPOLLRDHUP)
        {
            c->peerClosed = true;
            dispatchConnectEvent(tcp, cid, TcpEventType::PEER_CLOSED, {});
            ++dispatched;

            c = getConnection(cid);
            if (!c) continue;
        }

        if (e & EPOLLERR)
        {
            int err = 0;
            socklen_t elen = sizeof(err);
            if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err != 0)
                c->stickyError = mapErrno(err);
            else
                c->stickyError = TcpError{TcpErrc::SYSTEM_ERROR, 0};

            dispatchConnectEvent(tcp, cid, TcpEventType::ERROR, c->stickyError);
            ++dispatched;
        }
    }

    return dispatched;
}
} // namespace transport::tcp
