#include <Logger.h>
#include <iostream>
#include <chrono>
#include <ctime>
#include <json.hpp>
#include <fstream>
#include <iomanip> // for std::put_time
#include <arpa/inet.h> // for inet_pton
#include <unistd.h> // for close
#include <cstring>
#include <atomic>

// Define CONFIG_FILE if not already defined
#ifndef CONFIG_FILE
#define CONFIG_FILE "../VirtualRouter/Configs/Configs.json"
#endif

// Implementation of Logger methods

Logger& Logger::getInstance()
{
    static Logger instance;
    return instance;
}

// Constructor
Logger::Logger()
    : serverIP_("127.0.0.1"), serverPort_(54000), minLogLevel_(LogLevel::INFO),
    timestampFormat_("%Y-%m-%d %H:%M:%S"), sock_(-1), running(false) {
}

// Destructor
Logger::~Logger()
{
    running = false;
    queueCV_.notify_all();
    if (senderThread_.joinable())
    {
        senderThread_.join();
    }
    if (sock_ != -1) {
        close(sock_);
    }
}

// Initialization
void Logger::initialize(bool start)
{
    if (!start) {
        return;
    }

    nlohmann::json configJson;
    std::string configPath = CONFIG_FILE;
    std::fstream configFile(configPath);
    if (configFile.is_open())
    {
        try
        {
            configFile >> configJson;
            configFile.close();
        }
        catch(nlohmann::json::parse_error &e)
        {
            std::cerr << "JSON Parse Error: " << e.what() << std::endl;
            return;
        }
    }
    else
    {
        std::cerr << "Failed to open file: " << configPath << std::endl;
        return;
    }

    if (configJson.contains("Logger"))
    {
        if (configJson["Logger"].contains("IP") && configJson["Logger"].contains("Port"))
        {
            serverIP_ = configJson["Logger"]["IP"].get<std::string>();
            int tempPort  = configJson["Logger"]["Port"].get<int>();
            if (tempPort < 0)
            {
                std::cerr << "Invalid port number in config file: " << tempPort << std::endl;
                serverPort_ = 54000; // Default port
            }
            else
            {
                serverPort_ = static_cast<uint16_t>(tempPort);
            }
        }
        else
        {
            std::cerr << "Logger configuration incomplete in config file." << std::endl;
            return;
        }

        // Log Level
        if (configJson["Logger"].contains("LogLevel"))
        {
            std::string levelStr = configJson["Logger"]["LogLevel"].get<std::string>();
            if (levelStr == "INFO")
            {
                minLogLevel_ = LogLevel::INFO;
            }
            else if (levelStr == "DEBUG")
            {
                minLogLevel_ = LogLevel::DEBUG;
            }
            else if(levelStr == "WARN")
            {
                minLogLevel_ = LogLevel::WARN;
            }
            else if (levelStr == "ERROR")
            {
                minLogLevel_ = LogLevel::ERROR;
            }
            else
            {
                std::cerr << "Unknown LogLevel in config file: " << levelStr << ". Using INFO" << std::endl;
                minLogLevel_ = LogLevel::INFO;
            }
        }

        // Timestamp Format
        if (configJson["Logger"].contains("TimestampFormat"))
        {
            std::string formatStr = configJson["Logger"]["TimestampFormat"].get<std::string>();
            // Validate the format string if necessary
            timestampFormat_ = formatStr;
        }
    }
    else
    {
        std::cerr << "Logger configuration missing in config file." << std::endl;
        return;
    }

    // Start the sender thread
    running = true;
    
    // Establish connection to the Log Server
    connectToServer();

    senderThread_ = std::thread(&Logger::processLogs, this);
}

// Connect to the Log Server
void Logger::connectToServer()
{
    // Create a socket (IPv4, TCP)
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0)
    {
        std::cerr << "Logger: Failed to create socket." << std::endl;
        exit(EXIT_FAILURE);
    }

    // Server address structure
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort_);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, serverIP_.c_str(), &serverAddr.sin_addr) <= 0)
    {
        std::cerr << "Logger: Connection to Log Server failed." << std::endl;
        close(sock_);
        running = false;
        return;
    }

    // Connect to the server
    if (connect(sock_, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        std::cerr << "Logger: Connection to Log Server failed" << std::endl;
        close (sock_);
        running = false;
        return;
    }

    std::cout << "Logger: Connected to Log Server at " << serverIP_ << ":" << serverPort_ << std::endl;
}

// Log a message with a specific level
void Logger::enqueue(const std::string& message, LogLevel level)
{
    // Filter messages below the minimal log level
    if (static_cast<int>(level) < static_cast<int>(minLogLevel_))
    {
        return; // Discard the message
    }
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        logQueue_.emplace(message, level);
    }
    queueCV_.notify_one();
}

// Process and send log messages
void Logger::processLogs()
{
    while (running)
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        queueCV_.wait(lock, [this]() { return !logQueue_.empty() || !running; });

        while (!logQueue_.empty())
        {
            auto [msg, level] = logQueue_.front();
            logQueue_.pop();
            lock.unlock();

            // Send the message
            ssize_t bytesSent = send(sock_, msg.c_str(), msg.size(), 0);
            if (bytesSent < 0) {
                std::cerr << "Logger: Failed to send log message. Attempting to reconnect..." << std::endl;
                close(sock_);
                connectToServer();
            }

            lock.lock();
        }
    }
}

// Convert LogLevel enum to string
std::string Logger::getLogLevelString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::INFO: return "INFO";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

// Get current timestamp as string
std::string Logger::getCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;

    // Convert to local time
    localtime_r(&now_time, &local_tm);

    // Format: Customizable via timestampFormat_
    std::ostringstream oss;
    oss << std::put_time(&local_tm, timestampFormat_.c_str());
    return oss.str();
}

// Implementation of LogStream methods

LogStream::LogStream(Logger& logger, LogLevel level)
    : logger_(logger), level_(level) {}

LogStream::~LogStream() {
    if (!logger_.running) { return; }
    std::string msg = stream_.str();
    // Trim any trailing newline
    if (!msg.empty() && msg.back() == '\n')
    {
        msg.pop_back();
    }

    // Format the message with timestamp and log level
    std::string formattedMsg = "[" + logger_.getCurrentTimestamp() + "] [" + logger_.getLogLevelString(level_) + "] " + msg + "\n";
    
    // Enqueue the formatted message with its level
    logger_.enqueue(formattedMsg, level_);
}

LogStream& LogStream::operator<<(std::ostream& (*manip)(std::ostream&))
{
    stream_ << manip;
    return *this;
}

// Logger level methods

LogStream Logger::info()
{
    return LogStream(*this, LogLevel::INFO);
}

LogStream Logger::debug()
{
    return LogStream(*this, LogLevel::DEBUG);
}

LogStream Logger::warn()
{
    return LogStream(*this, LogLevel::WARN);
}

LogStream Logger::error()
{
    return LogStream(*this, LogLevel::ERROR);
}