// MockConsole.hpp

#ifndef MOCK_CONSOLE_HPP
#define MOCK_CONSOLE_HPP

#include <cli/runtime/Console.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace cli
{
class MockConsole : public ConsoleController
{
public:
    MOCK_METHOD(void, clearScreen, (), (override));
    MOCK_METHOD(void, enableLineWrapping, (), (override));
    MOCK_METHOD(void, clearLineAfterCursor, (), (override));
    MOCK_METHOD(void, saveCursorPosition, (), (override));
    MOCK_METHOD(void, restoreCursorPosition, (), (override));
    MOCK_METHOD(void, moveCursorToStart, (), (override));
    MOCK_METHOD(void, moveCursorLeft, (size_t count), (override));
    MOCK_METHOD(void, moveCursorRight, (size_t count), (override));
    MOCK_METHOD(void, moveCursorUp, (size_t count), (override));
    MOCK_METHOD(void, moveCursorDown, (size_t count), (override));
    MOCK_METHOD(void, print, (const std::string& str, Color color), (override));
    MOCK_METHOD(CursorPosition, getCursorPosition, (), (override));
    MOCK_METHOD(void, beep, (), (override)); // Remove or comment out if not used
    MOCK_METHOD(void, flush, (), (override));

    size_t getTerminalWidth() override
    {
        return terminalWidth;
    }

    void setTerminalWidth(size_t width)
    {
        terminalWidth = width;
    }


    // Constructor to set default behavior
    MockConsole()
    {
        // Set the default behavior of the 'print' method to append to 'capturedOutput'
        ON_CALL(*this, print(::testing::_, ::testing::_))
            .WillByDefault([this](const std::string& str, Color) {
                capturedOutput += str; // Append the string to 'capturedOutput'
            });
    }

    // Utility to reset captured output for testing purposes
    void resetCapturedOutput()
    {
        capturedOutput.clear();
    }

    // Utility to get the captured output
    const std::string& getCapturedOutput() const
    {
        return capturedOutput;
    }

private:

    // String to store all printed output
    std::string capturedOutput;

    // Default terminal width
    size_t terminalWidth = 80;
};

// Reduced Mock class for IConsole using Google Mock
class ReducedMockConsole : public ConsoleController
{
public:
    MOCK_METHOD(void, print, (const std::string&, Color), (override));
    void clearScreen() override {}
    void enableLineWrapping() override {}
    void clearLineAfterCursor() override {}
    void saveCursorPosition() override {}
    void restoreCursorPosition() override {}
    void moveCursorToStart() override {}
    void moveCursorLeft(size_t) override {}
    void moveCursorRight(size_t) override {}
    void moveCursorUp(size_t) override {}
    void moveCursorDown(size_t) override {}
    CursorPosition getCursorPosition() override {return CursorPosition{};}
    void beep() override {}
    void flush() override {}

    size_t getTerminalWidth() override
    {
        return terminalWidth;
    }

    void setTerminalWidth(size_t width)
    {
        terminalWidth = width;
    }

    // Constructor to set default behavior
    ReducedMockConsole()
    {
        // Set the default behavior of the 'print' method to append to 'capturedOutput'
        ON_CALL(*this, print(::testing::_, ::testing::_))
            .WillByDefault([this](std::string str, Color) {
                capturedOutput += str; // Append the string to 'capturedOutput'
            });
    }

    // Utility to reset captured output for testing purposes
    void resetCapturedOutput()
    {
        capturedOutput.clear();
    }

    // Utility to get the captured output
    const std::string& getCapturedOutput() const
    {
        return capturedOutput;
    }

private:

    // String to store all printed output
    std::string capturedOutput;

    // Default terminal width
    size_t terminalWidth = 80;
};
}

#endif // MOCK_CONSOLE_HPP
