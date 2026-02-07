// Tcp.cpp

#include "Tcp.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <unordered_map>
#include <vector>

class VirtualRouter;

namespace TCP
{
static TcpError mapErrno(int e) noexcept
{
    TcpError out{};
    out.osErrno = e;

    switch (e)
    {
        case 0: out.code = TcpErrc::OK; break;

        case EWOULDBLOCK: out.code = TcpErrc::WOULD_BLOCK; break;

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
    return e.code != TcpErrc::OK &&
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

static TcpResult<void> applyPolicy(int fd, int af, const TcpSocketPolicy& p) noexcept
{
    if (p.lowLatency)
    {
        int one = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0)
            return TcpResult<void>{ mapErrno(errno) };
    }

    if (p.keepAlive)
    {
        int one = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one)) != 0)
            return TcpResult<void>{ mapErrno(errno) };
    }

    if (p.tos.has_value())
    {
        int v = static_cast<int>(*p.tos);
        if (af == AF_INET)
        {
            if (setsockopt(fd, IPPROTO_IP, IP_TOS, &v, sizeof(v)) != 0)
                return TcpResult<void>{ mapErrno(errno) };
        }
        else if (af == AF_INET6)
        {
            if (setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &v, sizeof(v)) != 0)
                return TcpResult<void>{ mapErrno(errno) };
        }
        else return TcpResult<void>{ TcpError{TcpErrc::INVALID_ARGUMENT, 0} };
    }

    if (p.ttl.has_value())
    {
        int v = static_cast<int>(*p.ttl);
        if (af == AF_INET)
        {
            if (setsockopt(fd, IPPROTO_IP, IP_TTL, &v, sizeof(v)) != 0)
                return TcpResult<void>{ mapErrno(errno) };
        }
        else if (af == AF_INET6)
        {
            if (setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &v, sizeof(v)) != 0)
                return TcpResult<void>{ mapErrno(errno) };
        }
        else return TcpResult<void>{ TcpError{TcpErrc::INVALID_ARGUMENT, 0} };
    }

    return TcpResult<void>{};
}

static TcpSocketPolicy mergePolicy(const TcpSocketPolicy& defaults, const TcpSocketPolicy& overrides) noexcept
{
    TcpSocketPolicy out = defaults;
    if (overrides.ttl.has_value()) out.ttl = overrides.ttl;
    if (overrides.tos.has_value()) out.tos = overrides.tos;
    out.lowLatency = overrides.lowLatency || defaults.lowLatency;
    out.keepAlive = overrides.keepAlive || defaults.keepAlive;
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

struct Tcp::TcpEngine final
{
    static constexpr uint64_t kListenerTag = (1ull << 63);
    static constexpr uint64_t kIdMask = ~kListenerTag;

    static uint64_t packListener(ListenId id) noexcept { return kListenerTag | (id & kIdMask); }
    static uint64_t packConn(ConnId id) noexcept { return (id & kIdMask); }
    static bool isListenerTag(uint64_t v) noexcept { return (v & kListenerTag) != 0; }
    static uint64_t unpackId(uint64_t v) noexcept { return (v & kIdMask); }

    explicit TcpEngine(VirtualRouter& v, Tcp::Config c)
        : vr(v), cfg(c)
    {
        epfd = epoll_create1(EPOLL_CLOEXEC);
        if (epfd < 0) epfd = -1;

        conns.reserve(cfg.maxConnections);
        conns.max_load_factor(0.70f);

        epScratch.reserve(256);
    }

    ~TcpEngine()
    {
        for (auto& [id, l] : listeners) if (l.fd >= 0) ::close(l.fd);
        for (auto& [id, c] : conns) if (c.fd >= 0) ::close(c.fd);
        if (epfd >= 0) ::close(epfd);
    }

    struct Listener final
    {
        ListenId id{0};
        int fd{-1};
        int af{AF_UNSPEC};

        TcpEndpoint local{};
        size_t backlog{0};

        TcpSocketPolicy policyApplied{};

        Tcp::AcceptCallback onAccept{nullptr};
        void* onAcceptUser{nullptr};

        Tcp::ConnCallback acceptedConnCallback{nullptr};
        void* acceptedConnUser{nullptr};

        std::vector<ConnId> pendingAcceptIds;
    };

    struct Conn final
    {
        ConnId id{0};
        int fd{-1};

        TcpSocketKey key{};
        bool connectPending{false};
        bool peerClosed{false};

        TcpError stickyError{};

        Tcp::ConnCallback cb{nullptr};
        void* cbUser{nullptr};
    };

    VirtualRouter& vr;
    Tcp::Config cfg;

    int epfd{-1};

    ListenId nextListenId{1};
    ConnId nextConnId{1};
    TcpPort nextEphemeral{0};

    std::unordered_map<ListenId, Listener> listeners;
    std::unordered_map<ConnId, Conn> conns;

    std::vector<epoll_event> epScratch;

    ListenId allocListenId() noexcept { return nextListenId++; }
    ConnId allocConnId() noexcept { return nextConnId++; }

    TcpPort allocEphemeral() noexcept
    {
        if (cfg.ephemeralMin > cfg.ephemeralMax) return 0;
        if (nextEphemeral < cfg.ephemeralMin || nextEphemeral > cfg.ephemeralMax)
            nextEphemeral = cfg.ephemeralMin;

        TcpPort p = nextEphemeral++;
        if (nextEphemeral > cfg.ephemeralMax) nextEphemeral = cfg.ephemeralMin;
        return p;
    }

    void epAdd(int fd, uint64_t tag, uint32_t events) noexcept
    {
        if (epfd < 0) return;
        epoll_event ev;
        ev.events = events;
        ev.data.u64 = tag;
        (void)epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    }

    void epDel(int fd) noexcept
    {
        if (epfd < 0) return;
        (void)epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    }

    TcpResult<void> bindToDeviceIfRequested(int fd, const TcpInterfaceBind& b) noexcept
    {
        if (!b.ifname || b.ifnameLen == 0) return TcpResult<void>{};

        if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, b.ifname, static_cast<socklen_t>(b.ifnameLen)) != 0)
        {
            return TcpResult<void>{ mapErrno(errno) };
        }
        return TcpResult<void>{};
    }

    TcpResult<void> bindEndpoint(int fd, const TcpEndpoint& ep) noexcept
    {
        sockaddr_storage ss{};
        uint32_t slen = 0;
        TcpIpAdapter::writeSocketaddr(ep.address, ep.port, &ss, &slen);
        
        if (::bind(fd, reinterpret_cast<sockaddr*>(&ss), static_cast<socklen_t>(slen)) != 0)
            return TcpResult<void>{ mapErrno(errno) };

        return TcpResult<void>{};
    }

    TcpResult<void> getLiveKey(int fd, TcpSocketKey& out) noexcept
    {
        sockaddr_storage lss{};
        sockaddr_storage rss{};
        socklen_t llen = sizeof(lss);
        socklen_t rlen = sizeof(rss);

        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&lss), &llen) != 0)
            return TcpResult<void>{ mapErrno(errno) };

        IPAddress lip{};
        TcpPort lport{};

        TcpIpAdapter::readSockaddr(&lss, static_cast<uint32_t>(llen), lip, lport);
        out.local = TcpEndpoint{lip, lport};

        if (::getpeername(fd, reinterpret_cast<sockaddr*>(&rss), &rlen) != 0)
        {
            if (errno == ENOTCONN)
            {
                out.remote = TcpEndpoint{};
                return TcpResult<void>{};
            }
            return TcpResult<void>{ mapErrno(errno) };
        }

        IPAddress rip{};
        TcpPort rport{};
        TcpIpAdapter::readSockaddr(&rss, static_cast<uint32_t>(rlen), rip, rport);
        out.remote = TcpEndpoint{rip, rport};
        return TcpResult<void>{};
    }

    TcpResult<TcpState> linuxState(int fd) const noexcept
    {
        tcp_info info{};
        socklen_t len = sizeof(info);
        if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &len) != 0)
            return TcpResult<TcpState>{ TcpState::CLOSED, mapErrno(errno) };

        return TcpResult<TcpState>{ mapLinuxTcpState(info.tcpi_state), {} };
    }

    TcpResult<ConnId> adoptAcceptedSocket(Listener& lst, int cfd) noexcept
    {
        if (conns.size() >= cfg.maxConnections)
        {
            ::close(cfd);
            return TcpResult<ConnId>{ 0, TcpError{TcpErrc::NO_RESOURCES, 0} };
        }

        {
            auto pr = applyPolicy(cfd, lst.af, lst.policyApplied);
            if (!pr)
            {
                TcpError e = pr.error;
                ::close(cfd);
                return TcpResult<ConnId>{ 0, e };
            }
        }

        ConnId cid = allocConnId();
        Conn c{};
        c.id = cid;
        c.fd = cfd;

        c.connectPending = false;

        c.cb = lst.acceptedConnCallback;
        c.cbUser = lst.acceptedConnUser;

        {
            TcpSocketKey k{};
            auto r = getLiveKey(cfd, k);
            if (!r) { ::close(cfd); return TcpResult<ConnId>{0, r.error}; }
            c.key = k;
        }

        conns.emplace(cid, std::move(c));

        if (epfd >= 0)
            epAdd(cfd, packConn(cid), EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET);

        return TcpResult<ConnId>{ cid, {} };
    }

    size_t drainAccept(Listener& lst, bool invokeCallbacks, Tcp* tcp) noexcept
    {
        size_t accepted = 0;

        while (true)
        {
            int cfd = ::accept4(lst.fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (cfd < 0)
            {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                break;
            }

            auto ar = adoptAcceptedSocket(lst, cfd);
            if (ar)
            {
                ++accepted;
                lst.pendingAcceptIds.push_back(ar.value);

                if (invokeCallbacks && tcp)
                {
                    if (lst.onAccept)
                    {
                        auto itc = conns.find(ar.value);
                        if (itc != conns.end())
                            lst.onAccept(lst.onAcceptUser, *tcp, lst.id, itc->second.id, itc->second.key);
                    }

                    auto itc = conns.find(ar.value);
                    if (itc != conns.end() && itc->second.cb)
                    {
                        TcpEvent ev{ TcpEventType::ACCEPTED, itc->second.id, {} };
                        itc->second.cb(itc->second.cbUser, *tcp, itc->second.id, ev);
                    }
                }
            }
            else
            {
                // adopt accepted socket
            }
        }
        return accepted;
    }

    void dispatchConnEvent(Tcp& tcp, ConnId cid, TcpEventType t, TcpError e) noexcept
    {
        auto itc = conns.find(cid);
        if (itc == conns.end()) return;
        auto& c = itc->second;

        if (!c.cb) return;

        TcpEvent ev{};
        ev.type = t;
        ev.id = cid;
        ev.error = e;
        c.cb(c.cbUser, tcp, cid, ev);
    }
};

Tcp::Tcp(VirtualRouter& vrf, Config cfg)
{
    engine = new TcpEngine(vrf, cfg);
}

Tcp::~Tcp()
{
    delete engine;
    engine = nullptr;
}

TcpResult<Tcp::ListenId> Tcp::listen(const TcpEndpoint& local, const ListenOptions& opt)
{
    if (!engine) return TcpResult<ListenId>{ 0, TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    const int af = TcpIpAdapter::af(local.address);
    if (af != AF_INET && af != AF_INET6)
        return TcpResult<ListenId>{ 0, TcpError{TcpErrc::INVALID_ARGUMENT, 0} };

    int fd = ::socket(af, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return TcpResult<ListenId>{ 0, mapErrno(errno) };

    if (setNonBlocking(fd) != 0) { ::close(fd); return TcpResult<ListenId>{0, mapErrno(errno)}; }

    {
        int one = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }

    {
        auto r = engine->bindToDeviceIfRequested(fd, opt.bind);
        if (!r) { ::close(fd); return TcpResult<ListenId>{0, r.error}; }
    }
    
    const TcpSocketPolicy p = mergePolicy(engine->cfg.defaults, opt.policy);
    {
        auto r = applyPolicy(fd, af, p);
        if (!r) { ::close(fd); return TcpResult<ListenId>{0, r.error}; }
    }

    {
        auto r = engine->bindEndpoint(fd, local);
        if (!r) { ::close(fd); return TcpResult<ListenId>{0, r.error}; }
    }

    if (::listen(fd, static_cast<int>(opt.backlog)) != 0)
    {
        TcpError e = mapErrno(errno);
        ::close(fd);
        return TcpResult<ListenId>{0, e};
    }

    ListenId id = engine->allocListenId();
    TcpEngine::Listener l{};
    l.id = id;
    l.fd = fd;
    l.local = local;
    l.backlog = opt.backlog;
    l.policyApplied = p;

    l.onAccept = opt.onAccept;
    l.onAcceptUser = opt.onAcceptUser;
    l.acceptedConnCallback = opt.acceptConnCallback;
    l.acceptedConnUser = opt.acceptedConnUser;

    engine->listeners.emplace(id, std::move(l));

    if (engine->epfd >= 0)
        engine->epAdd(fd, TcpEngine::packListener(id), EPOLLIN | EPOLLET);

    return TcpResult<ListenId>{ id, {} };
}

TcpResult<void> Tcp::unlisten(ListenId id)
{
    if (!engine) return TcpResult<void>{ TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    auto it = engine->listeners.find(id);
    if (it == engine->listeners.end())
        return TcpResult<void>{ TcpError{TcpErrc::NOT_FOUND, 0} };

    int fd = it->second.fd;
    if (engine->epfd >= 0) engine->epDel(fd);

    ::close(fd);
    engine->listeners.erase(it);

    return TcpResult<void>{};
}

TcpResult<Tcp::ConnId> Tcp::accept(ListenId id)
{
    if (!engine) return TcpResult<ConnId>{ 0, TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    auto it = engine->listeners.find(id);
    if (it == engine->listeners.end())
        return TcpResult<ConnId>{ 0, TcpError{TcpErrc::NOT_FOUND, 0} };

    auto& lst = it->second;

    if (!lst.pendingAcceptIds.empty())
    {
        ConnId cid = lst.pendingAcceptIds.back();
        lst.pendingAcceptIds.pop_back();
        return TcpResult<ConnId>{ cid, {} };
    }

    engine->drainAccept(lst, false, nullptr);

    if (!lst.pendingAcceptIds.empty())
    {
        ConnId cid = lst.pendingAcceptIds.back();
        lst.pendingAcceptIds.pop_back();
        return TcpResult<ConnId>{ cid, {} };
    }

    return TcpResult<ConnId>{0, TcpError{TcpErrc::WOULD_BLOCK, EWOULDBLOCK} };
}

TcpResult<Tcp::ConnId> Tcp::connect(const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opt)
{
    if (!engine) return TcpResult<ConnId>{ 0, TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    const int af = TcpIpAdapter::af(remote.address);
    if (af != AF_INET && af != AF_INET6)
        return TcpResult<ConnId>{ 0, TcpError{TcpErrc::INVALID_ARGUMENT, 0} };

    int fd = ::socket(af, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return TcpResult<ConnId>{ 0, mapErrno(errno) };

    if (setNonBlocking(fd) != 0) { ::close(fd); return TcpResult<ConnId>(0, mapErrno(errno)); }

    {
        auto r = engine->bindToDeviceIfRequested(fd, opt.bind);
        if (!r) { ::close(fd); return TcpResult<ConnId>{0, r.error}; }
    }

    const TcpSocketPolicy p = mergePolicy(engine->cfg.defaults, opt.policy);
    {
        auto r = applyPolicy(fd, af, p);
        if (!r) { ::close(fd); return TcpResult<ConnId>{0, r.error}; }
    }

    TcpEndpoint bindEp = local;

    if (bindEp.port == 0)
    {
        if (engine->cfg.ephemeralMin > engine->cfg.ephemeralMax)
        {
            ::close(fd);
            return TcpResult<ConnId>{ 0, TcpError{TcpErrc::INVALID_ARGUMENT, 0} };
        }

        const uint32_t range = static_cast<uint32_t>(engine->cfg.ephemeralMax - engine->cfg.ephemeralMin + 1);
        bool bound = false;

        for (uint32_t i = 0; i < range; ++i)
        {
            bindEp.port = engine->allocEphemeral();
            auto br = engine->bindEndpoint(fd, bindEp);

            if (br) { bound = true; break; }

            if (br.error.code != TcpErrc::ADDRESS_IN_USE)
            {
                ::close(fd);
                return TcpResult<ConnId>{ 0, br.error };
            }
        }

        if (!bound)
        {
            ::close(fd);
            return TcpResult<ConnId>{ 0, TcpError{TcpErrc::NO_RESOURCES, 0} };
        }
    }
    else
    {
        auto br = engine->bindEndpoint(fd, bindEp);
        if (!br) { ::close(fd); return TcpResult<ConnId>{ 0, br.error }; }
    }

    // connect
    sockaddr_storage rss{};
    uint32_t rlen = 0;
    TcpIpAdapter::writeSocketaddr(remote.address, remote.port, &rss, &rlen);

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&rss), static_cast<socklen_t>(rlen));
    bool pending = false;

    if (rc != 0)
    {
        if (errno != EINPROGRESS)
        {
            TcpError e = mapErrno(errno);
            ::close(fd);
            return TcpResult<ConnId>{ 0, e };
        }
        pending = true;
    }
    
    if (engine->conns.size() >= engine->cfg.maxConnections)
    {
        ::close(fd);
        return TcpResult<ConnId>{ 0, TcpError{TcpErrc::NO_RESOURCES, 0} };
    }

    ConnId cid = engine->allocConnId();
    TcpEngine::Conn c{};
    c.id = cid;
    c.fd = fd;
    c.connectPending = pending;

    c.cb = opt.callback;
    c.cbUser = opt.callbackUser;

    c.key.local = bindEp;
    c.key.remote = remote;

    {
        TcpSocketKey live{};
        if (engine->getLiveKey(fd, live))
            c.key.local = live.local;
    }

    engine->conns.emplace(cid, std::move(c));

    if (engine->epfd >= 0)
        engine->epAdd(fd, TcpEngine::packConn(cid), EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET);

    return TcpResult<ConnId>{ cid, {} };
}

TcpResult<size_t> Tcp::send(ConnId id, std::span<const uint8_t> data)
{
    if (!engine) return TcpResult<size_t>{ 0, TcpError{TcpErrc::SYSTEM_ERROR, 0} };
    
    auto it = engine->conns.find(id);
    if (it == engine->conns.end())
        return TcpResult<size_t>{0, TcpError{TcpErrc::NOT_FOUND, 0} };

    auto& c = it->second;
    if (!c.stickyError.ok()) return TcpResult<size_t>{ 0, c.stickyError };

    while (true)
    {
        ssize_t n = ::send(c.fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (n < 0)
        {
            if (errno == EINTR) continue;

            TcpError e = mapErrno(errno);
            if (shouldStickify(e)) c.stickyError = e;
            return TcpResult<size_t>{0, e};
        }
        return TcpResult<size_t>{ static_cast<size_t>(n), {} };
    }
}

TcpResult<size_t> Tcp::recv(ConnId id, std::span<uint8_t> out)
{
    if (!engine) return TcpResult<size_t>{ 0, TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    auto it = engine->conns.find(id);
    if (it == engine->conns.end())
        return TcpResult<size_t>{ 0, TcpError{TcpErrc::NOT_FOUND, 0} };

    auto& c = it->second;
    if (!c.stickyError.ok()) return TcpResult<size_t>{0, c.stickyError };

    while (true)
    {
        ssize_t n = ::recv(c.fd, out.data(), out.size(), 0);
        if (n < 0)
        {
            if (errno == EINTR) continue;

            TcpError e = mapErrno(errno);
            if (shouldStickify(e)) c.stickyError = e;
            return TcpResult<size_t>{ 0, e };
        }

        if (n == 0) c.peerClosed = true;
        return TcpResult<size_t>{ static_cast<size_t>(n), {} };
    }
}

TcpResult<void> Tcp::shutdown(ConnId id, TcpShutdown how)
{
    if (!engine) return TcpResult<void>{ TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    auto it = engine->conns.find(id);
    if (it == engine->conns.end())
        return TcpResult<void>{ TcpError{TcpErrc::NOT_FOUND, 0} };

    int sh = SHUT_RDWR;
    if (how == TcpShutdown::READ) sh = SHUT_RD;
    else if (how == TcpShutdown::WRITE) sh = SHUT_WR;

    if (::shutdown(it->second.fd, sh) != 0)
        return TcpResult<void>{ mapErrno(errno) };

    return TcpResult<void>{};
}

TcpResult<void> Tcp::close(ConnId id)
{
    if (!engine) return TcpResult<void>{ TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    auto it = engine->conns.find(id);
    if (it == engine->conns.end())
        return TcpResult<void>{ TcpError{TcpErrc::NOT_FOUND, 0} };

    int fd = it->second.fd;
    if (engine->epfd >= 0) engine->epDel(fd);

    ::close(fd);
    engine->conns.erase(it);

    return TcpResult<void>{};
}

TcpResult<TcpState> Tcp::state(ConnId id) const
{
    if (!engine) return TcpResult<TcpState> { TcpState::CLOSED, TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    auto it = engine->conns.find(id);
    if (it == engine->conns.end())
        return TcpResult<TcpState>{ TcpState::CLOSED, TcpError{TcpErrc::NOT_FOUND, 0} };

    if (!it->second.stickyError.ok())
        return TcpResult<TcpState>{ TcpState::CLOSED, it->second.stickyError };

    return engine->linuxState(it->second.fd);
}

TcpResult<std::optional<TcpSocketKey>> Tcp::socketKey(ConnId id) const
{
    if (!engine) return TcpResult<std::optional<TcpSocketKey>>{ std::nullopt, TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    auto it = engine->conns.find(id);
    if (it == engine->conns.end())
        return TcpResult<std::optional<TcpSocketKey>>{ std::nullopt, TcpError{TcpErrc::NOT_FOUND, 0} };

    TcpSocketKey live{};
    auto r = engine->getLiveKey(it->second.fd, live);
    if (!r) return TcpResult<std::optional<TcpSocketKey>>{ std::nullopt, r.error };

    return TcpResult<std::optional<TcpSocketKey>>{ live, {} };
}

TcpResult<size_t> Tcp::pollEvents(std::span<TcpEvent> outEvents, uint32_t timeoutMs)
{
    if (!engine) return TcpResult<size_t>{ 0, TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    if (engine->epfd < 0)
        return TcpResult<size_t>{ 0, TcpError{TcpErrc::NOT_SUPPORTED, 0} };

    if (outEvents.empty())
        return TcpResult<size_t>{ 0, TcpError{TcpErrc::INVALID_ARGUMENT, 0} };

    // one epoll_wait, then translate into portable TcpEvents
    engine->epScratch.resize(outEvents.size());

    int n = ::epoll_wait(engine->epfd, engine->epScratch.data(), static_cast<int>(engine->epScratch.size()),
                         static_cast<int>(timeoutMs));
    if (n < 0)
    {
        if (errno == EINTR) return TcpResult<size_t>{ 0, {} };
        return TcpResult<size_t>{ 0, mapErrno(errno) };
    }

    size_t produced = 0;

    for (int i = 0; i < n && produced < outEvents.size(); ++i)
    {
        const uint32_t e = engine->epScratch[i].events;
        const uint64_t tag = engine->epScratch[i].data.u64;

        // Listener: accept readiness
        if (TcpEngine::isListenerTag(tag))
        {
            ListenId lid = static_cast<ListenId>(TcpEngine::unpackId(tag));

            auto lit = engine->listeners.find(lid);
            if (lit == engine->listeners.end()) continue;

            engine->drainAccept(lit->second, false, nullptr);

            if (!lit->second.pendingAcceptIds.empty())
                outEvents[produced++] = TcpEvent{ TcpEventType::ACCEPT_READY, lid, {} };

            continue;
        }

        ConnId cid = static_cast<ConnId>(TcpEngine::unpackId(tag));
        auto itc = engine->conns.find(cid);
        if (itc == engine->conns.end()) continue;

        auto& c = itc->second;

        // Connection
        if (c.connectPending && (e & (EPOLLOUT | EPOLLERR | EPOLLHUP)))
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
            // capture SO_ERROR into sticky error
            int err = 0;
            socklen_t elen = sizeof(err);
            if (getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err != 0)
                c.stickyError = mapErrno(err);
            else
                c.stickyError = TcpError{TcpErrc::SYSTEM_ERROR, 0};

            outEvents[produced++] = TcpEvent{ TcpEventType::ERROR, c.id, c.stickyError };
        }
    }

    return TcpResult<size_t>{ produced, {} };
}

TcpResult<size_t> Tcp::pump(uint32_t timeoutMs, size_t maxEvents)
{
    if (!engine) return TcpResult<size_t>{ 0, TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    if (engine->epfd < 0)
        return TcpResult<size_t>{ 0, TcpError{TcpErrc::NOT_SUPPORTED, 0} };

    if (maxEvents == 0)
        return TcpResult<size_t>{ 0, TcpError{TcpErrc::INVALID_ARGUMENT, 0} };

    engine->epScratch.resize(maxEvents);

    int n = ::epoll_wait(engine->epfd, engine->epScratch.data(), static_cast<int>(engine->epScratch.size()),
                         static_cast<int>(timeoutMs));
    if (n < 0)
    {
        if (errno == EINTR) return TcpResult<size_t>{ 0, {} };
        return TcpResult<size_t>{ 0, mapErrno(errno) };
    }

    size_t dispatched = 0;

    for (int i = 0; i < n; ++i)
    {
        const uint32_t e = engine->epScratch[i].events;
        const uint64_t tag = engine->epScratch[i].data.u64;

        if (TcpEngine::isListenerTag(tag))
        {
            ListenId lid = static_cast<ListenId>(TcpEngine::unpackId(tag));
            auto lit = engine->listeners.find(lid);
            if (lit == engine->listeners.end()) continue;

            // drain and invoke callbacks
            // drainAccept itself dispatches ACCEPTED + onAccept hooks per connection
            dispatched += engine->drainAccept(lit->second, true, this);

            continue;
        }

        ConnId cid = static_cast<ConnId>(TcpEngine::unpackId(tag));
        auto itc = engine->conns.find(cid);
        if (itc == engine->conns.end()) continue;

        auto& c = itc->second;

        // connect completion
        if (c.connectPending && (e & (EPOLLOUT | EPOLLERR | EPOLLHUP)))
        {
            int err = 0;
            socklen_t elen = sizeof(err);
            if (getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0)
            {
                if (err == 0)
                {
                    c.connectPending = false;
                    engine->dispatchConnEvent(*this, cid, TcpEventType::CONNECTED, {});
                    ++dispatched;
                }
                else
                {
                    c.stickyError = mapErrno(err);
                    engine->dispatchConnEvent(*this, cid, TcpEventType::ERROR, c.stickyError);
                    ++dispatched;
                    continue;
                }
            }
            else
            {
                c.stickyError = mapErrno(errno);
                engine->dispatchConnEvent(*this, cid, TcpEventType::ERROR, c.stickyError);
                ++dispatched;
                continue;
            }
        }

        if (e & EPOLLIN)
        {
            engine->dispatchConnEvent(*this, cid, TcpEventType::READABLE, {});
            ++dispatched;
        }

        if (e & EPOLLOUT)
        {
            engine->dispatchConnEvent(*this, cid, TcpEventType::WRITABLE, {});
            ++dispatched;
        }

        if (e & EPOLLRDHUP)
        {
            // mark state
            auto itc2 = engine->conns.find(cid);
            if (itc2 != engine->conns.end()) itc2->second.peerClosed = true;

            engine->dispatchConnEvent(*this, cid, TcpEventType::PEER_CLOSED, {});
            ++dispatched;
        }

        if (e & EPOLLERR)
        {
            auto itc2 = engine->conns.find(cid);
            if (itc2 == engine->conns.end()) continue;

            int err = 0;
            socklen_t elen = sizeof(err);
            if (getsockopt(itc2->second.fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err != 0)
                itc2->second.stickyError = mapErrno(err);
            else
                itc2->second.stickyError = TcpError{TcpErrc::SYSTEM_ERROR, 0};

            engine->dispatchConnEvent(*this, cid, TcpEventType::ERROR, itc2->second.stickyError);
            ++dispatched;
        }
    }

    return TcpResult<size_t>{ dispatched, {} };
}

TcpResult<void> Tcp::setCallback(ConnId id, ConnCallback cb, void* user)
{
    if (!engine) return TcpResult<void>{ TcpError{TcpErrc::SYSTEM_ERROR, 0} };

    auto it = engine->conns.find(id);
    if (it == engine->conns.end())
        return TcpResult<void>{ TcpError{TcpErrc::NOT_FOUND, 0} };

    it->second.cb = cb;
    it->second.cbUser = user;
    return TcpResult<void>{};
}

TcpResult<void> Tcp::input(const TcpSegment&)
{
    // Kernel backend cannot accept injected TCP segments. It is not a limitation of this wrapper;
    // it is the kernel owning TCP parse/state/output.
    return TcpResult<void>{ TcpError{TcpErrc::NOT_SUPPORTED, 0} };
}
}
