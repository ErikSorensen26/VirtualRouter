// LogServer.cpp

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <cstring>
#include <json.hpp>
#include <fstream>
#include <sstream>
#include <atomic>

// POSIX Socket Headers
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#define CONFIG_FILE "../VirtualRouter/Configs/Configs.json"

// Constants
const int DEFAULT_PORT = 54000;
const int BUFFER_SIZE = 4096;

// Is server running
std::atomic<bool> serverRunning(true);

// Mutex for synchronized colsole output
std::mutex countMutex;

// Function to handle each client side connection
void handleClient(int clientSocket, sockaddr_in clientAddr)
{
    char host[NI_MAXHOST];
    char svc[NI_MAXSERV];

    // Get the client's IP address and port
    inet_ntop(AF_INET, &(clientAddr.sin_addr), host, NI_MAXHOST);
    int clientPort = ntohs(clientAddr.sin_port);

    {
        std::lock_guard<std::mutex> lock(countMutex);
        std::cout << "Connected to " << host << ":" << clientPort << std::endl;
    }

    std::string incomingData; // Buffer to hold incomplete data
    char buffer[BUFFER_SIZE];
    
    while (serverRunning)
    {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
        if (bytesReceived <= 0)
        {
            break;
        }

        // Ensure null-termination
        buffer[bytesReceived] = '\0';
        incomingData += buffer;

        // Process complete lines
        size_t pos;
        while ((pos = incomingData.find("\n")) != std::string::npos)
        {
            std::string line = incomingData.substr(0, pos);
            incomingData.erase(0, pos + 1); // Remove the processed line

            size_t firstBracket = line.find('[');
            size_t firstClose = line.find(']', firstBracket + 1);

            size_t secondBracket = line.find('[', firstBracket + 1);
            size_t secondClose = line.find(']', secondBracket + 1);

            std::string timestamp = "UNKNOWN";
            std::string logLevel = "UNKNOWN";
            std::string message = line; // Default to full line if parsing fails

            if (firstBracket != std::string::npos && firstClose != std::string::npos &&
                secondBracket != std::string::npos && secondClose != std::string::npos )
            {
                timestamp = line.substr(firstBracket + 1, firstClose - firstBracket - 1);
                logLevel = line.substr(secondBracket + 1, secondClose - secondBracket - 1);
                message = line.substr(secondClose + 1); // Extract the actual message
            }

            // Trim leading spaces from message
            size_t start = message.find_first_not_of(" ");
            if (start != std::string::npos)
            {
                message = message.substr(start);
            }

            // Display based on log level
            {
                std::lock_guard<std::mutex> lock(countMutex);
                if (logLevel == "INFO")
                {
                    std::cout << "[" << timestamp << "] [INFO] " << message << std::endl;
                }
                else if (logLevel == "DEBUG")
                {
                    std::cout << "[" << timestamp << "] [DEBUG] " << message << std::endl;
                }
                else if (logLevel == "WARN")
                {
                    std::cout << "[" << timestamp << "] [WARN] " << message << std::endl;
                }
                else if (logLevel == "ERROR")
                {
                    std::cout << "[" << timestamp << "] [ERROR] " << message << std::endl;
                }
                else
                {
                    std::cout << "[" << timestamp << "] [UNKNOWN] " << message << std::endl;
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(countMutex);
        std::cout << "Disconnected from " << host << ":" << clientPort << std::endl;
    }

    close(clientSocket);
}

int main()
{

    nlohmann::json configJson;
    std::string configFilename = CONFIG_FILE;
    std::ifstream configFile(configFilename);
    if (configFile.is_open())
    {
        try
        {
            configFile >> configJson;
            configFile.close();
        }
        catch(nlohmann::json::parse_error& e)
        {
            std::cerr << "JSON Parse Error: " << e.what() << std::endl;
            return -1;
        }
    }
    else
    {
        std::cerr << "Failed to open file: " << configFilename << std::endl;
        return -1;
    }

    // Determine port from configuration or use default
    int PORT = DEFAULT_PORT;
    if (configJson.contains("Logger") && configJson["Logger"].contains("Port"))
    {
        int tempPort = configJson["Logger"]["Port"].get<int>();
        if (tempPort > 0 && tempPort < 65536)
        {
            PORT = tempPort;
        }
        else
        {
            std::cerr << "Invalid port number in config file. Using default port " << DEFAULT_PORT << "." << std::endl;
        }
    }

    // Create a socket (IPv4, TCP)
    int listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listeningSocket < 0)
    {
        std::cerr << "Error: Cannot create socket" << std::endl;
        return -1;
    }

    // Bind the socket to an IP/Port
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    memset(&(serverAddr.sin_zero), '0', 8);

    if (bind(listeningSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        std::cerr << "Error: Bind failed." << std::endl;
        close(listeningSocket);
        return -1;
    } 

    // Listen for incoming connections
    if (listen(listeningSocket, 10) < 0)
    {
        std::cerr << "Error: Listen failed." << std::endl;
        close(listeningSocket);
        return -1;
    }

    std::cout << "Log Server is running on port " << PORT << ". Waiting for connections..." << std::endl;

    std::vector<std::thread> clientThreads;

    while (serverRunning)
    {
        sockaddr_in clientAddr;
        socklen_t clientSize = sizeof(clientAddr);
        int clientSocket = accept(listeningSocket, (sockaddr*)&clientAddr, &clientSize);
        if (clientSocket < 0)
        {
            if (!serverRunning) break; // Exit if shutdown signal received
            std::cerr << "Error: Accept failed" << std::endl;
            continue;
        }

        // Spawn a new thread to handle the client
        clientThreads.emplace_back(std::thread(handleClient, clientSocket, clientAddr));
    }

    // Close the listening socket
    close(listeningSocket);

    // Join all client threads before exiting (unreachable)
    for (auto& th : clientThreads)
    {
        if (th.joinable())
        {
            th.join();
        }
    }

    std::cout << "Log Server shutdown complete." << std::endl;
    return 0;
}