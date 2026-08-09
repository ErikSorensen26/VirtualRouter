/**
 * @file Logger.h
 * @brief Logging infrastructure with levels and output control.
 */

/**
 * @defgroup UTILS Utilities
 * @brief Cross-cutting utilities: logging, byte manipulation, timing, RCU, and event dispatch.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include <sstream>

namespace utils
{

/**
 * @brief Logging infrastructure: severity levels, output control, formatting.
 * @ingroup UTILS
 *
 * Centralized logging with configurable severity (DEBUG, INFO, WARNING, ERROR, CRITICAL),
 * multiple output destinations (syslog, file, console), and rate limiting.
 */
class LogStream;

/**
 * @enum LogLevel
 * @brief Enumerates the various levels of logging severity.
 * @ingroup UTILS
 */
enum class LogLevel 
{
    INFO,   ///< Informationsl messages.
    DEBUG_LEVEL,  ///< Debugging messages.
    WARN,   ///< Warning messages.
    ERROR   ///< Error messages.
};

/**
 * @class ILogger
 * @brief Virtual class used for used for the logger while testing.
 *
 * The ILogger class provides virtual functions for Logging levels so that
 * logs can be identified during tests.
 */
class ILogger
{
public:
    virtual ~ILogger() = default;
    virtual LogStream info(bool isolate = false) = 0;
    virtual LogStream debug(bool isolate = false) = 0;
    virtual LogStream warn(bool isolate = false) = 0;
    virtual LogStream error(bool isolate = false) = 0;
};

/**
 * @class Logger
 * @brief Singleton class responsible for logging messages to a remote Log Server.
 * @ingroup UTILS
 *
 * The Logger class provides thread-safe logging capabilities with different severity levels.
 * It supports asynchronous message sending using a dedicated sender thread and handles
 * connection management with the Log Server.
 */
class Logger : public ILogger
{
public:

    /**
     * @brief Retrieves the singleton instance of the Logger.
     * @ingroup UTILS
     *
     * @return Logger& Reference to the Logger instance.
     */
    static Logger& getInstance();

    // Method to enqueue a log message
    void enqueue(const std::string& message, LogLevel level);

    /**
     * @brief Initializes the Logger with configuration settings.
     *
     * Reads the configuration from a JSON file, sets up logging parameters,
     * establishes a connection to the Log Server, and starts the sender thread.
     *
     * @param start Boolean flag indicating whether to start the Logger.
     * @param isolateMode Boolean flag indicating if the Logger should operate in isolated mode.
     */
    void initialize(bool start, bool isolateMode = false);

    /**
     * @brief Destructor for the Logger class.
     *
     * Signals the sender thread to stop, waits for it to finish,
     * and closes the socket connection to the Log Server.
     */
    ~Logger();

    /**
     * @brief Logs an informational message.
     *
     * @param isolate Boolean flag indicating if the message should be isolated.
     * @return LogStream Object to handle the message stream.
     */
    LogStream info(bool isolate = false) override;

    /**
     * @brief Logs a debug message.
     *
     * @param isolate Boolean flag indicating if the message should be isolated.
     * @return LogStream Object to handle the message stream.
     */
    LogStream debug(bool isolate = false) override;

    /**
     * @brief Logs a warning message.
     *
     * @param isolate Boolean flag indicating if the message should be isolated.
     * @return LogStream Object to handle the message stream.
     */
    LogStream warn(bool isolate = false) override;

    /**
     * @brief Logs an error message.
     *
     * @param isolate Boolean flag indicating if the message should be isolated.
     * @return LogStream Object to handle the message stream.
     */
    LogStream error(bool isolate = false) override;

    /**
     * @brief Converts a LogLevel enum to its string representation.
     *
     * @param level The LogLevel to convert.
     * @return std::string The string representation of the log level.
     */
    std::string getLogLevelString(LogLevel level);

    /**
     * @brief Retrieves the current timestamp as a formatted string.
     *
     * @return std::string The formatted current timestamp.
     */
    std::string getCurrentTimestamp();

    std::atomic<bool> running; ///< Flag indicating if the logger is running.

    /**
     * @brief Disallows copying of the Logger singleton.
     */
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

private:

    /**
     * @brief Constructor for the Logger class.
     *
     * Initializes member variables with default values.
     */
    Logger();

    /**
     * @brief Establishes a connection to the Log Server.
     *
     * Creates a TCP socket, sets up the server address, and attempts to connect.
     * If the connection fails, logs an error and sets the running flag to false.
     */
    void connectToServer();

    /**
     * @brief Processes log messages in the queue and sends them to the Log Server.
     *
     * Runs in a separate thread, waiting for messages to be enqueued and sending them over the socket.
     * Handles reconnection attempts if sending fails.
     */
    void processLogs();

    
    // Members
    std::string serverIP_;                  ///< IP address of the server log
    uint16_t serverPort_;                   ///< Port number of the Log Server
    LogLevel minLogLevel_;                  ///< Minimum log level to be recorded.
    std::string timestampFormat_;           ///< Format string for timestamp
    int sock_;                              ///< Socket file descriptor for the connection.
    std::thread senderThread_;              ///< Thread responsible for sending log messages
    std::queue<std::pair<std::string, LogLevel>> logQueue_; ///< Queue to hold log messages.
    std::mutex queueMutex_;                 ///< Mutex to protect access to the log queue.
    std::condition_variable queueCV_;       ///< Condition variable to notify the sender thread.
    bool isolatedMode = false;              ///< Flag indicating if the Logger is in isolation mode.
};

/**
 * @class LogStream
 * @brief Helper class to handle streaming of log messages.
 *
 * Collects log message parts and formats them upon destruction.
 */
class LogStream 
{
public:

    /**
     * @brief Constructs a LogStream object.
     *
     * @param logger Reference to the Logger instance.
     * @param level LogLevel indicating the severity of the message.
     * @param isFiltered Boolean flag indicating if the message should be filtered.
     */
    LogStream(Logger& logger, LogLevel level, bool isFiltered = false);

    /**
     * @brief Destructor for the LogStream class.
     *
     * Formats and enqueues the complete log message to the Logger.
     */
    ~LogStream();

    /**
     * @brief Appends any streamable value to the pending log message.
     *
     * @param value Value to format into the message.
     * @return Reference to this LogStream, for chaining.
     */
    template <typename T>
    LogStream& operator<<(const T& value)
    {
        stream_ << value;
        return *this;
    }

    /**
     * @brief Applies a stream manipulator (e.g. std::endl) to the message.
     *
     * @param ostream Manipulator function to apply.
     * @return Reference to this LogStream, for chaining.
     */
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&));

    bool filtered; ///< Flag indicating if the message is filtered.

private:
    Logger& logger_; ///< Reference to the logger instance.
    LogLevel level_; ///< Severity level of the log message.
    std::ostringstream stream_; ///< Stream to collect message parts.
};

} // namespace utils

#endif // LOGGER_H

