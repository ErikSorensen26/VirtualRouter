#pragma once

#include <string>
#include <libtelnet.h>


class TelnetManager
{
public:
    TelnetManager();
    ~TelnetManager();

    void Connect(const std::string& ip, int port);
    void SendCommand(const std::string& command);
    void Disconnect();

private:
    telnet_t* telnetSession;
    int sockfd;

    static void telnetCallback(telnet_t* telnet, telnet_event_t* event, void* userData);
};
