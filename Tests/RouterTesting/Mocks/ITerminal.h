#pragma once

#include <string>

// Struct to represent cursor position
struct CursorPosition
{
    int row;
    int col;
};

class ITerminal
{
public:
    virtual ~ITerminal() = default;

    // Clears the terminal screen and moves the cursor to home
    virtual void clearScreen() = 0;

    // Enable line wrapping
    virtual void enableLineWrapping() = 0;

    // Clears from the cursor to the end of the line
    virtual void clearLineAfterCursor() = 0;

    // Saves the current cursor position
    virtual void saveCursorPosition() = 0;

    // Restores the cursor position
    virtual void restoreCursorPosition() = 0;

    // Moves the cursor left by 'count' positions
    virtual void moveCursorLeft(int count = 1) = 0;

    // Moves the cursor right by 'count' positions
    virtual void moveCursorRight(int count = 1) = 0;

    // Moves the cursor up by 'count' positions
    virtual void moveCursorUp(int count = 1) = 0;

    // Moves the cursor down by 'count' positions
    virtual void moveCursorDown(int count = 1) = 0;

    // Prints a string to the terminal
    virtual void print(const std::string& str) = 0;

    // Gets the current cursor position
    virtual CursorPosition getCursorPosition() = 0;

    // Beep sound
    virtual void beep() = 0;
};
