// TelnetClient.hpp

#ifndef TELNET_CLIENT_HPP
#define TELNET_CLIENT_HPP

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <termio.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace utils::telnet
{
// TELNET CONSTANTS

static constexpr uint8_t TELNET_IAC  = 255;
static constexpr uint8_t TELNET_DONT = 254;
static constexpr uint8_t TELNET_DO   = 253;
static constexpr uint8_t TELNET_WONT = 252;
static constexpr uint8_t TELNET_WILL = 251;
static constexpr uint8_t TELNET_SB   = 250; // subnegotiation begin
static constexpr uint8_t TELNET_SE   = 250; // subnegotiation end

static constexpr uint8_t TELNET_OPT_ECHO = 1;
static constexpr uint8_t TELNET_OPT_SGA  = 3;
static constexpr uint8_t TELNET_OPT_NAWS = 31;

struct RawMode
{
    termios saved{};
    bool active = false;

    void enable()
    {
        if (!isatty(STDIN_FILENO)) return;
        tcgetattr(STDIN_FILENO, &saved);
        termios raw = saved;
        cfmakeraw(&raw);
        raw.c_oflag |= OPOST;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        active = true;
    }

    ~RawMode()
    {
        if (active) tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
    }
};

static int connectTo(const std::string& host, uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (rc != 0)
        throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(rc));

    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) throw std::runtime_error("Could not connect to " + host);
    return fd;
}

static bool sendAll(int fd, const uint8_t* buf, size_t len)
{
    while (len > 0)
    {
        ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
        if (n <= 0) return false;
        buf += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

class IacParser
{
public:
    enum class State { DATA, IAC_RECV, OPT_CMD, SB_DATA, SB_IAC };

    struct Event
    {
        enum class Kind { DATA, NEGO, SB };
        Kind kind    = Kind::DATA;
        uint8_t byte = 0; // DATA: the plain byte
        uint8_t cmd  = 0; // NEGO/SB: WILL/WONT/DO/DONT or SB
        uint8_t opt  = 0;
    };

    // Fee done byte; return zero or one event.
    bool feed(uint8_t byte, Event& ev)
    {
        switch (state)
        {
            case State::DATA:
            {
                if (byte == TELNET_IAC)
                {
                    state = State::IAC_RECV;
                    return false;
                }
                ev = {Event::Kind::DATA, byte, 0, 0};
                return true;
            }
            case State::IAC_RECV:
            {
                if (byte == TELNET_IAC)
                {
                    state = State::DATA;
                    ev = {Event::Kind::DATA, 0xFF, 0, 0};
                    return true;
                }
                if (byte == TELNET_SB)
                {
                    state = State::SB_DATA;
                    sbOpt = 0;
                    return false;
                }
                if (byte == TELNET_WILL || byte == TELNET_WONT || byte == TELNET_DO || byte == TELNET_DONT)
                {
                    cmd = byte;
                    state = State::OPT_CMD;
                    return false;
                }
                state = State::DATA;
                return false;
            }
            case State::OPT_CMD:
            {
                ev = {Event::Kind::NEGO, 0, cmd, byte};
                state = State::DATA;
                return true;
            }
            case State::SB_DATA:
            {
                if (byte == TELNET_IAC)
                {
                    state = State::SB_IAC;
                    return false;
                }
                return false;
            }
            case State::SB_IAC:
            {
                if (byte == TELNET_SE)
                {
                    state = State::DATA;
                    return false;
                }
                state = State::SB_DATA;
                return false;
            }
        }
        return false;
    }

private:
    State state   = State::DATA;
    uint8_t cmd   = 0;
    uint8_t sbOpt = 0;
};

class TelnetSession
{
public:
    TelnetSession(int fd) : fd(fd) {}

    bool processServerByte(uint8_t byte)
    {
        IacParser::Event ev;
        if (!parser.feed(byte, ev))
            return true;

        switch (ev.kind)
        {
            case IacParser::Event::Kind::DATA:
            {
                std::cout.put(static_cast<char>(ev.byte));
                if (ev.byte == '\n' || ev.byte == '\r') std::cout.flush();
                break;
            }
            case IacParser::Event::Kind::NEGO:
            {
                handleNego(ev.cmd, ev.opt);
                break;
            }
            case IacParser::Event::Kind::SB:
            {
                break;
            }
        }
        return true;
    }

    void flushCout()
    {
        std::cout.flush();
    }

private:
    void handleNego(uint8_t cmd, uint8_t opt)
    {
        uint8_t replay[3] = {TELNET_IAC, 0, opt};

        if (cmd == TELNET_WILL)
        {
            if (opt == TELNET_OPT_ECHO || opt == TELNET_OPT_SGA)
                replay[1] = TELNET_DO;
            else
                replay[1] = TELNET_DONT;
        }
        else if (cmd == TELNET_DO)
            replay[1] = TELNET_WONT;
        else if (cmd == TELNET_WONT)
            replay[1] = TELNET_DONT;
        else
            replay[1] = TELNET_WONT;

        sendAll(fd, replay, sizeof(replay));
    }

    int fd;
    IacParser parser;
};

static std::atomic<bool> gRunning{true};

static void readerThread(int fd, TelnetSession& session)
{
    uint8_t buf[4096];
    while (gRunning.load(std::memory_order_relaxed))
    {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
        {
            gRunning.store(false);
            break;
        }
        for (ssize_t i = 0; i < n; ++i)
            session.processServerByte(buf[i]);
        session.flushCout();
    }
    std::cerr << "\r\n[Connection closed by server]\r\n";
}

static void writerThread(int fd)
{
    uint8_t byte;
    while (gRunning.load(std::memory_order_relaxed))
    {
        ssize_t n = read(STDIN_FILENO, &byte, 1);
        if (n <= 0)
        {
            gRunning.store(false);
            break;
        }

        if (byte == TELNET_IAC)
        {
            uint8_t pair[2] = {TELNET_IAC, TELNET_IAC};
            if (!sendAll(fd, pair, 2))
            {
                gRunning.store(false);
                break;
            }
        }
        else
        {
            if (!sendAll(fd, &byte, 1))
            {
                gRunning.store(false);
                break;
            }
        }
    }
    gRunning.store(false);
}

void static startTelnet(std::string host, uint16_t port)
{
    
}
}

#endif // TELNET_CLIENT_HPP
