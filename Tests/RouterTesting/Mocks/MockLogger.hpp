// MockLogger.hpp

#ifndef MOCK_LOGGER_HPP
#define MOCK_LOGGER_HPP

#include <Logger.h>
#include <gmock/gmock.h>
#include <sstream>

/**
 * @class MockLogger
 * @brief Mock class for the global logger class.
 */
class MockLogger : public ILogger
{
    MOCK_METHOD(LogStream, info, (bool isolate), (override));
    MOCK_METHOD(LogStream, debug, (bool isolate), (override));
    MOCK_METHOD(LogStream, warn, (bool isolate), (override));
    MOCK_METHOD(LogStream, error, (bool isolate), (override));
};

#endif // MOCK_LOGGER_HPP
