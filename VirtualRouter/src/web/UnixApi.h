// UnixApi.h

#ifndef UNIX_API_H
#define UNIX_API_H

#include <json.hpp>
#include <functional>
#include <unordered_map>
#include <string>
#include <cstdint>

class UnixApi
{
public:
    using Handler = std::function<void(int, const nlohmann::json&, UnixApi&)>;
    using ConnCB = std::function<void(int, const UnixApi&)>;

    UnixApi() = default;
    ~UnixApi();

    bool bindAndListen(const std::string& path, int backlog = 256);
    bool adoptListenSocket(int listenFd);

    void pollOnce(int timeoutMs);
    bool sendJson(int clientFd, const nlohmann::json& j);
    void closeClient(int clientFd);
    void stop();

    void setHandler(Handler h) { handler = std::move(h); }
    void setOnOpen(ConnCB cb) { onOpen = std::move(cb); }
    void setOnClose(ConnCB cb) { onClose = std::move(cb); }

    int listenFd() const { return lfd; }
    int epollFd() const { return epfd; }

private:
    struct Conn
    {
        std::string rbuf;
        std::string wbuf;
    };

    std::string sockPath;

    int lfd = -1;
    int epfd = -1;

    Handler handler;
    ConnCB onOpen;
    ConnCB onClose;

    std::unordered_map<int, Conn> conns;

    bool makeNonblock(int fd);
    bool addEpoll(int fd, uint32_t events);
    bool modEpoll(int fd, uint32_t events);
    void delEpoll(int fd);

    void handleAccept();
    void handleRead(int cfd);
    void handleWrite(int cfd);
    void drainWriteBuffer(int cfd);

    static bool popLine(std::string& buf, std::string& line);
};

#endif // UNIX_API_H
