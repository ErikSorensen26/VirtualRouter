// WebApi.h

#ifndef WEB_API_H
#define WEB_API_H

#include <nlohmann/json.hpp>
#include <functional>
#include <unordered_map>
#include <string>
#include <cstdint>

class WebApi
{
public:
    using Handler = std::function<void(int clientFd, const nlohmann::json& msg, WebApi& self)>;
    using ConnCB = std::function<void(int clientFd, const WebApi& self)>;

    WebApi() = default;
    ~WebApi();

    bool bindAndListen(uint16_t port, int backlog = 256);
    bool adoptListenSocket(int listen_fd);
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

    int lfd = -1; // Listening socket
    int epfd = -1; // Epoll instance;

    Handler handler;
    ConnCB onOpen;
    ConnCB onClose;

    std::unordered_map<int, Conn> conns;

    bool makeNodeblock(int fd);
    bool addEpoll(int fd, uint32_t events);
    bool modEpoll(int fd, uint32_t events);
    void delEpoll(int fd);

    void handleAccept();
    void handleRead(int cfd);
    void handleWrite(int cfd);
    void drainWriteBuffer(int cfd);

    static bool popLine(std::string& buf, std::string& like);
};

#endif // WEB_API_H
