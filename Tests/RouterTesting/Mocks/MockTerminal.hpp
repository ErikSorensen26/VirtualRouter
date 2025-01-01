#pragma once

#include "ITerminal.h"
#include <gmock/gmock.h>

// Mock class for ITerminal using Google Mock
class MockTerminal : public ITerminal
{
public:
    MOCK_METHOD(void, clearScreen, (), (override));
    MOCK_METHOD(void, enableLineWrapping, (), (override));
    MOCK_METHOD(void, clearLineAfterCursor, (), (override));
    MOCK_METHOD(void, saveCursorPosition, (), (override));
    MOCK_METHOD(void, restoreCursorPosition, (), (override));
    MOCK_METHOD(void, moveCursorLeft, (int count), (override));
    MOCK_METHOD(void, moveCursorRight, (int count), (override));
    MOCK_METHOD(void, moveCursorUp, (int count), (override));
    MOCK_METHOD(void, moveCursorDown, (int count), (override));
    MOCK_METHOD(void, print, (const std::string& str), (override));
    MOCK_METHOD(CursorPosition, getCursorPosition, (), (override));
    MOCK_METHOD(void, beep, (), (override)); // Remove or comment out if not used
};
