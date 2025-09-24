#include "UnixApi.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include <cerrno>
#include <cstring>
#include <iostream>

static void perrorMsg(const char* msg)
{
    std::cerr << msg << ": " << std::strerror(errno) << "\n";
}

UnixApi::~UnixApi() { stop(); }

bool UnixApi::makeNonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return false;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
}

bool UnixApi::addEpoll(int fd, uint32_t events)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool UnixApi::modEpoll(int fd, uint32_t events)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == 0;
}

void UnixApi::delEpoll(int fd)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}

bool UnixApi::bindAndListen(const std::string& path, int backlog)
{
    stop();
    sockPath = path;

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) { perrorMsg("epoll_create1"); return false; }

    lfd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (lfd < 0) { perrorMsg("socket"); return false; }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    socklen_t addrlen;
    if (!path.empty() && path[0] == '@')
    {
        addr.sun_path[0] = '\0';
        std::strncpy(addr.sun_path + 1, path.c_str() + 1, sizeof(addr.sun_path) - 2);
        addrlen = offsetof(sockaddr_un, sun_path) + 1 + std::strlen(path.c_str() + 1);
    }
    else
    {
        if (path.size() >= sizeof(addr.sun_path))
        {
            std::cerr << "UDS path too long\n";
            return false;
        }
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        ::unlink(path.c_str());
        addrlen = offsetof(sockaddr_un, sun_path) + std::strlen(addr.sun_path);
    }

    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), addrlen) < 0)
    {
        perrorMsg("bind");
        ::close(lfd);
        lfd = -1;
        return false;
    }

    if (!makeNonblock(lfd)) { perrorMsg("fcntl(O_NONBLOCK)"); return false; }
    if (::listen(lfd, backlog) < 0) { perrorMsg("listen"); return false; }

    if (!addEpoll(lfd, EPOLLIN | EPOLLET))
    {
        perrorMsg("epoll_ctl ADD listen");
        return false;
    }

    return true;
}

bool UnixApi::adoptListenSocket(int listenFd)
{
    stop();
    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) { perrorMsg("epoll_create1"); return false; }
    lfd = listenFd;
    if (!makeNonblock(lfd)) { perrorMsg("fcntl(O_NONBLOCK)"); return false; }
    if (!addEpoll(lfd, EPOLLIN|EPOLLET)) { perrorMsg("epoll_ctl ADD listen"); return false; }
    return true;
}

void UnixApi::stop()
{
    for (auto& [fd, _] : conns)
    {
        if (onClose) onClose(fd, *this);
        delEpoll(fd);
        ::close(fd);
    }
    conns.clear();

    if (lfd >= 0)
    {
        if (!sockPath.empty() && sockPath[0] != '@')
            ::unlink(sockPath.c_str());
        delEpoll(lfd);
        ::close(lfd);
        lfd = -1;
    }
    if (epfd >= 0) { ::close(epfd); epfd = -1; }
}

void UnixApi::handleAccept()
{
    while (true)
    {
        int cfd = ::accept4(lfd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
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
            perrorMsg("epoll_ctl Add client");
            ::close(cfd);
            conns.erase(cfd);
        }
        else
        {
            if (onOpen) onOpen(cfd, *this);
        }
    }
}

bool UnixApi::popLine(std::string& buf, std::string& line)
{
    auto pos = buf.find('\0');
    if (pos == std::string::npos) return false;
    line.assign(buf, 0, pos);
    buf.erase(0, pos + 1);
    return true;
}

void UnixApi::handleRead(int cfd)
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
                std::cout << "Received: " << line.size() << "\n";
                for (unsigned char ch : line) {
                    if (std::isprint(ch)) std::cout << ch;
                    else std::cout << "\\x" << std::hex << (int)ch << std::dec;
                }
                std::cout << "\n";
                try
                {
                    std::cout << "LINE: " << line << std::endl;
                    nlohmann::json msg = nlohmann::json::parse(line);
                    handler(cfd, msg, *this);
                }
                catch (...) {}
            }
            continue;
        }
        if (n == 0) { closeClient(cfd); return; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        perrorMsg("recv");
        closeClient(cfd);
        return;
    }
}

void UnixApi::drainWriteBuffer(int cfd)
{
    auto it = conns.find(cfd);
    if (it == conns.end()) return;
    Conn& c = it->second;

    while (!c.wbuf.empty())
    {
        const char* buf = c.wbuf.data();
        size_t len = c.wbuf.size();
        if (len == 0) break;

        ssize_t n = ::send(cfd, buf, len, MSG_NOSIGNAL);
        if (n > 0)
        {
            c.wbuf.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n == 0 || (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
        {
            modEpoll(cfd, EPOLLIN|EPOLLOUT|EPOLLET);
            return;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EPIPE)
        {
            closeClient(cfd);
            return;
        }

        perrorMsg("send");
        closeClient(cfd);
        return;
    }

    modEpoll(cfd, EPOLLIN|EPOLLET);
}

void UnixApi::handleWrite(int cfd)
{
    drainWriteBuffer(cfd);
}

bool UnixApi::sendJson(int clientFd, const nlohmann::json& j)
{
    auto it = conns.find(clientFd);
    if (it == conns.end()) return false;
    Conn& c = it->second;

    std::string s = j.dump();
    s.push_back('\0');

    if (!c.wbuf.empty())
    {
        c.wbuf.append(s);
        modEpoll(clientFd, EPOLLIN|EPOLLOUT|EPOLLET);
        return true;
    }

    ssize_t n = ::send(clientFd, s.data(), s.size(), MSG_NOSIGNAL);
    if (n == (ssize_t)s.size()) return true;

    if (n > 0 && n < (size_t)s.size())
    {
        c.wbuf.assign(s.data() + n, s.size() - (size_t)n);
        modEpoll(clientFd, EPOLLIN|EPOLLOUT|EPOLLET);
        return true;
    }
     
    if (n == 0 || (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
    {
        c.wbuf = s;
        modEpoll(clientFd, EPOLLIN|EPOLLOUT|EPOLLET);
        return true;
    }

    if (n < 0 && errno == EPIPE)
    {
        closeClient(clientFd);
        return false;
    }

    perrorMsg("send");
    closeClient(clientFd);
    return false;
}

void UnixApi::closeClient(int clientFd)
{
    auto it = conns.find(clientFd);
    if (it == conns.end()) return;

    conns.erase(it);

    delEpoll(clientFd);
    ::close(clientFd);

    if (onClose) onClose(clientFd, *this);
}

void UnixApi::pollOnce(int timeoutMs)
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
            if (events & EPOLLIN) handleAccept();
        }
        else
        {
            if (events & (EPOLLERR | EPOLLHUP)) { closeClient(fd); continue; }
            if (events & EPOLLIN) handleRead(fd);
            if (events & EPOLLOUT) handleWrite(fd);
        }
    }
}
