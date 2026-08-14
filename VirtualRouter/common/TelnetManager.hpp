// TelnetClient.hpp
#ifndef TELNETCLIENT_HPP
#define TELNETCLIENT_HPP

#include <iostream>
#include <cstring>
#include <string>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <libtelnet.h>
#include <cerrno>
#include <vector>
#include <sstream>

//std::vector<std::string> operator/(const std::string& s, char delimiter) {
    //std::vector<std::string> lines;
    //std::istringstream stream(s);
    //std::string line;
    //while (std::getline(stream, line, delimiter)) {
        //lines.push_back(line);
    //}
    //return lines;
//}

class TelnetClient {
public:
    TelnetClient() : sockfd(-1), telnetSession(nullptr) {}

    ~TelnetClient() {
        disconnect();
    }

    // Connect to a Telnet server (e.g., port 23).
    void connect(const std::string &ip, int port) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
            throw std::runtime_error("Failed to create socket");

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) {
            close(sockfd);
            throw std::runtime_error("Invalid IP address");
        }

        if (::connect(sockfd, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
            close(sockfd);
            throw std::runtime_error("Connection failed");
        }

        // Initialize libtelnet with an empty options array (disables negotiation)
        const telnet_telopt_t options[] = { {-1, 0, 0} };
        telnetSession = telnet_init(options, TelnetCallback, 0, this);
        if (!telnetSession)
            throw std::runtime_error("Failed to initialize telnet session");

        // Allow initial server output (e.g., login prompts)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Clear any initial output.
        readOutput(std::chrono::milliseconds(500),
                   std::chrono::seconds(2),
                   std::chrono::milliseconds(0));
    }

    std::string sendCommand(const std::string &command,
                            bool noReturn = false,
                            std::chrono::milliseconds idleTimeout = std::chrono::milliseconds(5),
                            std::chrono::milliseconds maxWaitTime = std::chrono::seconds(10),
                            std::chrono::milliseconds minWaitTime = std::chrono::milliseconds(50)) {
        if (!telnetSession)
            throw std::runtime_error("Not connected");

        // Clear previous output.
        outputBuffer.clear();

        // Append CRLF and send the command.
        std::string cmd = command + (noReturn ? "" : "\r\n");
        telnet_send(telnetSession, cmd.c_str(), cmd.size());
        // Optionally force processing.
        telnet_recv(telnetSession, "", 0);

        // Harvest output until we have been idle for idleTimeout,
        // but ensure at least minWaitTime has passed.
        return readOutput(idleTimeout, maxWaitTime, minWaitTime);
    }

    // Disconnect from the Telnet server.
    void disconnect() {
        if (telnetSession) {
            telnet_free(telnetSession);
            telnetSession = nullptr;
        }
        if (sockfd >= 0) {
            close(sockfd);
            sockfd = -1;
        }
    }

    // Returns and clears the current output buffer
    std::string readSome(std::chrono::milliseconds idleTimeout = std::chrono::milliseconds(100),
                         std::chrono::milliseconds maxWaitTime = std::chrono::seconds(2))
    {
        outputBuffer.clear();
        return readOutput(idleTimeout, maxWaitTime, std::chrono::milliseconds(0));
    }

private:
    int sockfd;
    telnet_t *telnetSession;
    std::string outputBuffer;

    // Callback from libtelnet.
    static void TelnetCallback(telnet_t* /*telnet*/, telnet_event_t* event, void* userData) {
        TelnetClient *client = static_cast<TelnetClient*>(userData);
        switch (event->type) {
            case TELNET_EV_DATA:
                client->outputBuffer.append(event->data.buffer, event->data.size);
                break;
            case TELNET_EV_SEND:
                if (send(client->sockfd, event->data.buffer, event->data.size, 0) < 0)
                    std::cerr << "Send error: " << strerror(errno) << std::endl;
                break;
            case TELNET_EV_ERROR:
                std::cerr << "Telnet error: " << event->error.msg << std::endl;
                break;
            default:
                // Ignore other events.
                break;
        }
    }

    // Read output from the socket until no new data is received for idleTimeout,
    // but ensure at least minWaitTime has passed (or maxWaitTime is reached).
    std::string readOutput(std::chrono::milliseconds idleTimeout,
                             std::chrono::milliseconds maxWaitTime,
                             std::chrono::milliseconds minWaitTime) {
        auto startTime = std::chrono::steady_clock::now();
        auto lastDataTime = startTime;
        char buffer[1024];

        while (true) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);
            // Use a short timeout for select, e.g., 200 ms.
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200 * 1000; // 200 ms

            int ret = select(sockfd + 1, &readfds, nullptr, nullptr, &tv);
            if (ret > 0 && FD_ISSET(sockfd, &readfds)) {
                int n = recv(sockfd, buffer, sizeof(buffer), 0);
                if (n > 0) {
                    // Pass data into libtelnet (which triggers the callback to append to outputBuffer).
                    telnet_recv(telnetSession, buffer, n);
                    lastDataTime = std::chrono::steady_clock::now();
                } else if (n == 0) {
                    // Connection closed.
                    break;
                } else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                        break;
                }
            }
            // Check if we've been idle and have waited at least the minimum time.
            auto now = std::chrono::steady_clock::now();
            if ((now - startTime >= minWaitTime) &&
                (now - lastDataTime >= idleTimeout))
                break;
            if (now - startTime >= maxWaitTime)
                break;
        }
        return outputBuffer;
    }
};

#endif // TELNETCLIENT_HPP
