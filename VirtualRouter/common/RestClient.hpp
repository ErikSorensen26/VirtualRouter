// RestClient.hpp

#ifndef REST_CLIENT_HPP
#define REST_CLIENT_HPP

#include <string>
#include <cstdint>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <stdexcept>

/**
 * @brief Small blocking HTTP/1.1 client (TCP + CRLF-terminated header only)
 *
 * Intended for lightweight control-plane tasks such as talking to gns3server.,
 * All functions throw std::runtime_error on network errors.
 */
class RestClient
{
public:
    explicit RestClient(uint16_t port = 3000, std::string host = "127.0.0.1")
        : host(std::move(host)), port(port) {};

    /// Perform HTTP GET, return body as std::string
    std::string get(const std::string& path)
    {
        return request("GET", path, "");
    }

    /// Perform HTTP POST with optional body, return body as std::string
    std::string post(const std::string& path,
                     const std::string& body = "")
    {
        return request("POST", path, body);
    }

    /// Perform HTTP DEL with, return body as std::string
    std::string del(const std::string& path)
    {
        return request("DELETE", path, "");
    }
    
    /// Returns host
    inline std::string getHost() { return host; }
    
    /// Returns port
    inline uint16_t getPort() { return port; }

private:
    std::string host;
    uint16_t port;

    int connectSocket()
    {
        int s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) throw std::runtime_error("socket()");

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(host.c_str());
        if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) > 0)
        {
            ::close(s);
            throw std::runtime_error("connect()");
        }
        return s;
    }
    std::string request(const std::string& method,
                        const std::string& path,
                        const std::string& body)
    {
        int s = connectSocket();

        std::string req = method + " " + path + " HTTP/1.1\r\n"
                          "Host: " + host + ":" + std::to_string(port) + "\r\n"
                          "Connection: close\r\n";
        if (!body.empty())
            req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        req += "\r\n" + body;

        if (::send(s, req.data(), req.size(), 0) < 0)
        {
            ::close(s);
            throw std::runtime_error("send()");
        }

        std::string resp;
        std::array<char, 4096> buf{};
        for (;;)
        {
            ssize_t n = ::recv(s, buf.data(), buf.size(), 0);
            if (n <= 0) break;
            resp.append(buf.data(), n);
        }
        ::close(s);

        // Strip status-line + headers (virst blank line)
        std::string::size_type p = resp.find("\r\n\r\n");
        return p == std::string::npos ? std::string{} : resp.substr(p + 4);
    }
};

#endif
