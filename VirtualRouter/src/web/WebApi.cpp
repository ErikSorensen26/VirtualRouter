// WebApi.cpp


#include "WebApi.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace web
{

static void perrorMsg(const char* msg)
{
    std::cerr << msg << ": " << std::strerror(errno) << "\n";
}

WebApi::~WebApi() { stop(); }

bool WebApi::makeNodeblock(int fd)
{
    int fl = fcntl(fd, F_SETFL, 0);
    if (fl < 0) return false;
    if (fcntl(fd, F_SETFL, fl|O_NONBLOCK) < 0) return false;
    return true;
}

bool WebApi::addEpoll(int fd, uint32_t events)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool WebApi::modEpoll(int fd, uint32_t events)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == 0;
}

void WebApi::delEpoll(int fd)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}

bool WebApi::bindAndListen(uint16_t port, int backlog)
{
    stop();

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) { perrorMsg("epoll_create1"); return false; }

    lfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (lfd < 0) { perrorMsg("socket"); return false; }

    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(lfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        perrorMsg("bind");
        ::close(lfd);
        lfd = -1;
        return false;
    }

    if (!makeNodeblock(lfd)) { perrorMsg("fcntl(O_NONBLOCK)"); return false; }
    if (listen(lfd, backlog) < 0) { perrorMsg("fcntl(O_NONBLOCK)"); return false; }

    if (!addEpoll(lfd, EPOLLIN|EPOLLET))
    {
        perrorMsg("epoll_ctl ADD listen");
        return false;
    }

    return true;
}

bool WebApi::adoptListenSocket(int listenFd)
{
    stop();
    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) { perrorMsg("epoll_create1"); return false; }
    lfd = listenFd;
    if (!makeNodeblock(lfd)) { perrorMsg("fcntl(O_NONBLOCK)"); return false; }
    if (!addEpoll(lfd, EPOLLIN|EPOLLET)) { perrorMsg("epoll_ctl ADD listen"); return false; }
    return true;
}

void WebApi::stop()
{
    for (auto& [fd, _] : conns)
    {
        if (onClose) onClose(fd, *this);
        delEpoll(fd);
        ::close(fd);
    }
    conns.clear();

    if (lfd >= 0) { delEpoll(lfd); ::close(lfd); lfd = -1; }
    if (epfd >= 0) { ::close(epfd); epfd = -1; }
}

void WebApi::handleAccept()
{
    while (true)
    {
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);
        int cfd = ::accept4(lfd, reinterpret_cast<sockaddr*>(&peer), &len, SOCK_NONBLOCK|SOCK_CLOEXEC);

        if (cfd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            perrorMsg("accept4");
            break;
        }
        conns.emplace(cfd, Conn{});
        if (!addEpoll(cfd, EPOLLIN|EPOLLET))
        {
            perrorMsg("epoll_ctl ADD client");
            ::close(cfd);
            conns.erase(cfd);
        }
        else
        {
            if (onOpen) onOpen(cfd, *this);
        }
    }
}

bool WebApi::popLine(std::string& buf, std::string& line)
{
    auto pos = buf.find('\n');
    if (pos == std::string::npos) return false;
    line.assign(buf.data(), pos);
    buf.erase(0, pos + 1);
    return true;
}

void WebApi::handleRead(int cfd)
{
    auto it = conns.find(cfd);
    if (it == conns.end()) return;
    Conn& c = it->second;

    char tmp[4096];
    while (true)
    {
        ssize_t n = ::recv(cfd, tmp, sizeof(tmp), 0);
        if (n > 0)
        {
            c.rbuf.append(tmp, tmp + n);
            std::string line;
            while (popLine(c.rbuf, line))
            {
                if (line.empty()) continue;
                if (!handler) continue;
                try
                {
                    nlohmann::json msg = nlohmann::json::parse(line);
                    handler(cfd, msg, *this);
                }
                catch (const nlohmann::json::parse_error& e)
                {
                    nlohmann::json err = {{"ok",  false}, {"error","invalid_json"}, {"what", e.what()}};
                    sendJson(cfd, err);
                }
            }
            continue;
        }
        if (n == 0)
        {
            closeClient(cfd);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        perrorMsg("recv");
        closeClient(cfd);
        return;
    }
}

void WebApi::drainWriteBuffer(int cfd)
{
    auto it = conns.find(cfd);
    if (it == conns.end()) return;
    Conn& c = it->second;

    while (!c.wbuf.empty())
    {
        ssize_t n = ::send(cfd, c.wbuf.data(), c.wbuf.size(), 0);
        if (n > 0)
        {
            c.wbuf.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            modEpoll(cfd, EPOLLIN | EPOLLOUT | EPOLLET);
            return;
        }
        if (n < 0 && errno == EINTR) continue;
        perrorMsg("send");
        closeClient(cfd);
        return;
    }
    
    modEpoll(cfd, EPOLLIN | EPOLLET);
}

void WebApi::handleWrite(int cfd)
{
    drainWriteBuffer(cfd);
}

bool WebApi::sendJson(int clientFd, const nlohmann::json& j)
{
    auto it = conns.find(clientFd);
    if (it == conns.end()) return false;
    Conn& c = it->second;

    std::string s = j.dump();
    s.push_back('\n');

    if (!c.wbuf.empty())
    {
        c.wbuf.append(s);
        modEpoll(clientFd, EPOLLIN | EPOLLOUT | EPOLLET);
        return true;
    }

    ssize_t n = ::send(clientFd, s.data(), s.size(), 0);
    if (n == static_cast<ssize_t>(s.size())) return true;

    if (n >= 0)
    {
        c.wbuf.assign(s.data() + n, s.size() - static_cast<size_t>(n));
        modEpoll(clientFd, EPOLLIN|EPOLLOUT|EPOLLET);
        return true;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
        c.wbuf = std::move(s);
        modEpoll(clientFd, EPOLLIN|EPOLLOUT|EPOLLET);
        return true;
    }

    perrorMsg("send");
    closeClient(clientFd);
    return false;
}

void WebApi::closeClient(int clientFd)
{
    auto it = conns.find(clientFd);
    if (it == conns.end()) return;

    if (onClose) onClose(clientFd, *this);

    delEpoll(clientFd);
    ::close(clientFd);
    conns.erase(it);
}

void WebApi::pollOnce(int timeoutMs)
{
    if (epfd < 0) return;

    constexpr int MAX_EVENTS = 64;
    epoll_event evs[MAX_EVENTS];

    int n = epoll_wait(epfd, evs, MAX_EVENTS, timeoutMs);
    if (n < 0)
    {
        if (errno == EINTR) return;
        perrorMsg("epoll_wait");
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        int fd = evs[i].data.fd;
        uint32_t events = evs[i].events;

        if (fd == lfd)
        {
            handleAccept();
        }

        if (events & (EPOLLERR | EPOLLHUP))
        {
            closeClient(fd);
            continue;
        }
        if (events & EPOLLIN) handleRead(fd);
        if (events & EPOLLOUT) handleWrite(fd);
    }
}

} // namespace web
