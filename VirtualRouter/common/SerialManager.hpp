// SerialManager.hpp

#ifndef SERIAL_MANAGER_HPP
#define SERIAL_MANAGER_HPP

#include <string>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <termio.h>

class SerialManager
{
public:
    SerialManager() : fd(-1) {}
    ~SerialManager() { disconnect(); }

    void connect(const std::string& device, int baudrate = 9600)
    {
        fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0)
            throw std::runtime_error("Failed to open serial device: " + device);

        termios tio{};
        if (tcgetattr(fd, &tio) != 0)
            throw std::runtime_error("tcgetattr failed");

        cfmakeraw(&tio);

        tio.c_cflag |= (CLOCAL | CREAD);
        tio.c_cflag &= ~CRTSCTS;
        tio.c_cflag &= ~CSTOPB;
        tio.c_cflag &= ~PARENB;
        tio.c_cflag &= ~CSIZE;
        tio.c_cflag |= CS8;

        speed_t speed = baudToFlag(baudrate);
        cfsetispeed(&tio, speed);
        cfsetospeed(&tio, speed);

        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;

        if (tcsetattr(fd, TCSANOW, &tio) != 0)
            throw std::runtime_error("tcsetattr failed");

        tcflush(fd, TCIOFLUSH);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        readOutput(std::chrono::milliseconds(200),
                   std::chrono::seconds(2),
                   std::chrono::milliseconds(0));
    }

    std::string sendCommand(const std::string& command, bool noReturn = false,
                            std::chrono::milliseconds idlTimeout = std::chrono::milliseconds(5),
                            std::chrono::milliseconds maxWaitTime = std::chrono::seconds(10),
                            std::chrono::milliseconds minWaitTime = std::chrono::milliseconds(50))
    {
        if (fd < 0)
            throw std::runtime_error("Not connected");

        outputBuffer.clear();
        
        std::string cmd = command + (noReturn ? "" : "\r\n");
        ssize_t written = write(fd, cmd.data(), cmd.size());
        if (written < 0)
            throw std::runtime_error("write failed");

        return readOutput(idlTimeout, maxWaitTime, minWaitTime);
    }

    std::string readSome(std::chrono::milliseconds idleTimeout = std::chrono::milliseconds(100),
                         std::chrono::milliseconds maxWaitTime = std::chrono::seconds(2))
    {
        outputBuffer.clear();
        return readOutput(idleTimeout, maxWaitTime, std::chrono::milliseconds(0));
    }

    void disconnect()
    {
        if (fd >= 0)
        {
            close(fd);
            fd = -1;
        }
    }

private:
    int fd;
    std::string outputBuffer;

    static speed_t baudToFlag(int baud)
    {
        switch (baud)
        {
            case 9600: return B9600;
            case 19200: return B19200;
            case 38400: return B38400;
            case 57600: return B57600;
            case 115200: return B115200;
            default:
                throw std::invalid_argument("Unsupported baud rate");
        }
    }

    std::string readOutput(std::chrono::milliseconds idleTimeout,
                           std::chrono::milliseconds maxWaitTime,
                           std::chrono::milliseconds minWaitTime)
    {
        auto startTime = std::chrono::steady_clock::now();
        auto lastDataTime = startTime;
        
        char buffer[1024];

        while (true)
        {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);

            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 200 * 1000;

            int ret = select(fd + 1, &rfds, nullptr, nullptr, &tv);
            if (ret > 0 && FD_ISSET(fd, &rfds))
            {
                ssize_t n = read(fd, buffer, sizeof(buffer));
                if (n > 0)
                {
                    outputBuffer.append(buffer, n);
                    lastDataTime = std::chrono::steady_clock::now();
                }
                else if (n < 0 && errno != EAGAIN)
                {
                    break;
                }
            }

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

#endif // SERIAL_MANAGER_HPP
