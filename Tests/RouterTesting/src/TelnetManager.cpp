#include <TelnetManager.h>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

// Telnet callback function
void TelnetManager::telnetCallback(telnet_t* telnet, telnet_event_t* event, void* userData) {
    int sockfd = *(int*)userData;

    switch (event->type) {
        case TELNET_EV_DATA:
            // Handle incoming Telnet data
            std::cout << "Received: " << std::string(event->data.buffer, event->data.size) << std::endl;
            break;

        case TELNET_EV_SEND:
            // Send data to the socket
            if (send(sockfd, event->data.buffer, event->data.size, 0) < 0) {
                std::cerr << "Failed to send data to Telnet server." << std::endl;
            }
            break;

        case TELNET_EV_ERROR:
            // Handle Telnet error
            std::cerr << "Telnet Error: " << event->error.msg << std::endl;
            break;

        default:
            break;
    }
}

TelnetManager::TelnetManager() : telnetSession(nullptr)
{

}

TelnetManager::~TelnetManager()
{
    Disconnect();
}

void TelnetManager::Connect(const std::string& ip, int port)
{
    sockaddr_in serverAddr{};

    // Create a socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        throw std::runtime_error("Failed to create socket.");
    }

    // Set up server address structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) < 0)
    {
        close(sockfd);
        throw std::runtime_error("Invalid IP address");
    }

    // Connect to the server
    if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        close(sockfd);
        throw std::runtime_error("Failed to connect to server.");
    }

    // Initialize the Telnet session
    const telnet_telopt_t telnetOptions[] = {
        {-1, 0, 0}
    };
    telnetSession = telnet_init(telnetOptions, telnetCallback, 0, &sockfd);
    if (!telnetSession)
    {
        close(sockfd);
        throw std::runtime_error("Failed to initialize Telnet session.");
    }

    std::cout << "Connect to " << ip << ":" << port << " via Telnet." << std::endl;
}

void TelnetManager::SendCommand(const std::string& command)
{
    if (!telnetSession || sockfd < 0)
    {
        throw std::runtime_error("Telnet session is not initialized.");
    }

    std::string commandWithNewline = command + "\r\n";
    telnet_send(telnetSession, commandWithNewline.c_str(), commandWithNewline.size());
}

void TelnetManager::Disconnect()
{
    if (telnetSession)
    {
        telnet_free(telnetSession);
        telnetSession = nullptr;
    }

    if (sockfd >= 0)
    {
        close(sockfd);
        sockfd = -1;
    }

    std::cout << "Disconnected from Telnet session." << std::endl;
}
