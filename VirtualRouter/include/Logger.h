#pragma once

#include <string>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include <sstream>
#include <functional>

class LogStream;

// Enumeration for log levels
enum class LogLevel {
    INFO = 0,
    DEBUG = 1,
    WARN = 2,
    ERROR = 3
};

class Logger {
public:
    // Public method to access the single instance (Singleton Pattern)
    static Logger& getInstance();

    // Methods to obtain LogStream for each log level
    LogStream info(bool isolate = false);
    LogStream debug(bool isolate = false);
    LogStream warn(bool isolate = false);
    LogStream error(bool isolate = false);

    // Method to enqueue a log message
    void enqueue(const std::string& message, LogLevel level);

    // Initialize the logger with configuration
    void initialize(bool start, bool isolateMode = false);

    // Destructor
    ~Logger();

    // Delete copy and move constructors and assignment operators
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    // Helper methods for LogStream
    std::string getLogLevelString(LogLevel level);
    std::string getCurrentTimestamp();

    std::atomic<bool> running;
private:
    // Private constructor to enforce Singleton Pattern
    Logger();

    // Internal method to establish connection to the Log Server
    void connectToServer();

    // Internal method running in a separate thread to send log messages
    void processLogs();

    // Isolate mode
    bool isolatedMode = false;
    
    // Members
    std::string serverIP_;
    uint16_t serverPort_;
    LogLevel minLogLevel_;
    std::string timestampFormat_;
    int sock_;
    std::thread senderThread_;
    std::queue<std::pair<std::string, LogLevel>> logQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCV_;
};

// LogStream class defination
class LogStream {
public:
    LogStream(Logger& logger, LogLevel level, bool isFiltered = false);
    ~LogStream();

    // Overload the insertion operator for various types
    template <typename T>
    LogStream& operator<<(const T& value)
    {
        stream_ << value;
        return *this;
    }

    // Overload for manipulators like std::endl;
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&));

    bool filtered;

private:
    Logger& logger_;
    LogLevel level_;
    std::ostringstream stream_;
};