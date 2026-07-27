// Console.cpp

#include <iostream>
#include <cstdio>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <vector>

#include "Console.h"

namespace cli
{
Console::Console(ConsoleController& term) : controller(term) {}

void Console::setPrompt(const std::string& newPrompt)
{
    prompt = newPrompt;
    cursorPos = 0;
    initialLineLength = prompt.length();

    // Clear current input on the screen and pring new prompt
    controller.print(prompt, Color::PROMPT);
    std::cout.flush();
}

bool Console::isCursorAtLineEnd()
{
    size_t width = getTerminalWidth();
    return (cursorPos + initialLineLength) % width == 0 && cursorPos != 0;
}

void Console::initConsole()
{
    // Clear the screen once
    controller.clearScreen(); // ANSI escape to clear and move cursor to top
    controller.enableLineWrapping(); // Enable line wrapping

    // Initialize inputBuffer as empty
    cursorPos = 0;

    // Print the prompt
    controller.print(prompt, Color::PROMPT);
    std::cout.flush();
}

void Console::clearLineAfterCursor() 
{
    size_t width = getTerminalWidth();
    controller.saveCursorPosition();
    controller.print(std::string(width, ' '));
    controller.restoreCursorPosition();
}

CursorPosition Console::getCursorPosition() 
{
    return controller.getCursorPosition();
}

bool Console::kbhit() 
{
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    int oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    int ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF)
    {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

size_t Console::getTerminalWidth() 
{
    return controller.getTerminalWidth();
}

void Console::moveCursorLeft(size_t steps) 
{
    for (size_t i = 0; i < steps; ++i)
    {
        if (cursorPos > 0)
        {
            auto tempCursorPos = cursorPos;
            if ((cursorPos + initialLineLength) % getTerminalWidth() == 0)
            {
                // Move to the previous line
                controller.moveCursorUp(1);
                controller.moveCursorRight(getTerminalWidth());
            }
            else
            {
                // Move left
                controller.moveCursorLeft(1);
            }
            cursorPos = tempCursorPos - 1;
        }
    }
}

void Console::moveCursorRight(size_t steps, std::string* input)
{
    size_t width = getTerminalWidth();
    for (size_t i = 0; i < steps; ++i)
    {
        if (!input || (input && cursorPos < (input->size())))
        {
            auto tempCursorPos = cursorPos;
            if ((cursorPos + initialLineLength) % width == width - 1)
            {
                // Move to the next line
                controller.moveCursorDown(1);
                controller.moveCursorToStart();
            }
            else
            {
                // Move right
                controller.moveCursorRight(1);
            }
            cursorPos = tempCursorPos + 1;
        }
    }
}

void Console::moveCursorUp(size_t steps) 
{
    if (steps > 0) 
    {
        controller.moveCursorUp(steps);
    }
}

void Console::moveCursorDown(size_t steps) 
{
    // Move cursor left 'cursorPos' times to reach the beginning
    if (steps > 0) 
    {
        controller.moveCursorDown(steps);
    }
}

void Console::moveCursorToStart()
{
    if (cursorPos > 0)
    {
        moveCursorLeft(cursorPos);
    }
}

void Console::moveCursorToEnd(std::string& input)
{
    // Move cursor right until the end is reached
    if (input.size() > cursorPos)
    {
        size_t steps = input.size() - cursorPos;
        moveCursorRight(steps);
    }
}

void Console::skipWordLeft(std::string& input)
{
    if (cursorPos == 0) return;

    size_t newPos = cursorPos - 1;

    if (newPos > 0 && input[newPos - 1] == ' ')
    {
        while (newPos > 0 && input[newPos - 1] == ' ')
        {
            newPos--;
        }
    }

    // Skip trailing spaces
    while (newPos > 0 && input[newPos - 1] != ' ')
    {
        newPos--;
    }
    size_t steps = cursorPos - newPos;
    moveCursorLeft(steps);
}

void Console::skipWordRight(std::string& input)
{
    if (cursorPos >= input.size()) return;

    size_t newPos = cursorPos;

    // Skip space if currently on one
    if (input[newPos] != ' ')
    {
        while (newPos < input.size() && input[newPos] != ' ')
        {
            newPos++;
        }
    }
    // Skip any spaces if on them
    while (newPos < input.size() && input[newPos] == ' ')
    {
        newPos++;
    }
    size_t steps = newPos - cursorPos;
    moveCursorRight(steps);
}

void Console::rewriteTail(const std::string& input, size_t startPosition, bool backspace)
{
    if (startPosition > input.size())
    {
        return;
    }
    
    size_t width = getTerminalWidth();
    size_t currentColumn = (startPosition + initialLineLength) % width;
    
    // Save the current cursor position
    controller.saveCursorPosition();

    // Rewrite the input from the start position
    for (size_t i = startPosition; i < (insert ? insertString.size() : input.size()); ++i)
    {
        if (currentColumn >= width)
        {
            controller.moveCursorDown(1);
            controller.moveCursorToStart();
            currentColumn = 0;
        }
        controller.print(std::string(1, (insert ? insertString : input)[i]));
    }

    // Clear any leftover characters on the current line and subsequent lines
    size_t leftoverSpace = (width > currentColumn) ? (width - currentColumn) : 0;
    if (leftoverSpace && leftoverSpace > 0)
    {
        controller.print(std::string(leftoverSpace, ' '));
    }

    // Clear leftover characters
    if (backspace)
    {
        controller.print(" ");
    }

    // Restore the cursor position
    controller.restoreCursorPosition();
}

std::string Console::input(std::string testInput, bool pagination)
{
    std::string input = inputCacheBuffer;

    // For loop and while loop, if testInput is empty it will act as a true while loop
    for (size_t i = 0; (testInput.empty()) || (i < testInput.length()); (testInput.empty()) ? (i) : (++i))
    {
        maxCommandLength = getTerminalWidth() - initialLineLength;

        // Non-blocking check for keypress
        if (kbhit() || !(testInput.empty()))
        {
            char hInput = testInput.empty() ? static_cast<char>(getchar()) : testInput[i];

            if (pagination)
            {
                return std::string(1, hInput);
            }

            // 1. Handle single-char special keys (Enter, Tab, '?', Backspace, Delete)
            std::string result = handleSpecialKey(hInput, input);
            if (!result.empty())
            {
                historyIndex = history.size();
                browsingHistory = false;
                inputCache.clear();
                // If we got a non-empty string, return now
                return result;
            }

            // 2. If it's an escape sequence
            if (hInput == '\x1b')
            {
                handleEscapeSequence(input);
            }

            // 3. If It's a normal printable char, handle in one go
            else if (std::isprint(static_cast<unsigned char>(hInput)))
            {
                handlePrintableChar(hInput, input);
            }

            if (!browsingHistory)
            {
                inputCache = input;
            }

            // Flush once per key
            std::cout.flush();
        }
    }
    return input;
}

std::string Console::handleSpecialKey(char hInput, std::string& input)
{
    auto backspace([&]()
    {
        cursorPos--;

        if ((cursorPos + initialLineLength + 1) % getTerminalWidth() == 0)
        {
            controller.moveCursorUp(1);
            controller.moveCursorRight(getTerminalWidth());
        }
        else
        {
            controller.moveCursorLeft(1);
        }

        if (!insert)
        {
            input.erase(cursorPos, 1);
            rewriteTail(input, cursorPos, true);
        }
        else
        {
            if (input.size() > cursorPos)
            {
                input[cursorPos] = insertString[cursorPos];
            }
            rewriteTail(insertString, cursorPos, true);
        }
    });

    switch (hInput)
    {
        case '\x3f': // '?'
        {
            // Print '?' and return, so we exit the input loop
            controller.print("?");
            input += "?";
            return input;
        }
        case '\x09': // Tab
        {
            // Return the input with 
            input += "\t";
            return input;
        }
        case '\x0a': // Enter
        {
            if (!input.empty() && input != lastCommand)
            {
                history.push_back(input);
            }
            lastCommand = input;
            if (input.empty())
            {
                return input + "\t";
            }
            else
            {
                return input;
            }
        }
        case '\x08': // Control-h
        case '\x15':
        {
            while (cursorPos > 0)
            {
                backspace();
            }
            break;
        }
        case '\x7f': // Backspace
        {
            if (cursorPos > 0)
            {
                backspace();
            }
            break;
        }
        default:
            // Not recognized => do nothing
            break;
    }
    // Return empty => keep reading input
    return "";
}

void Console::handleEscapeSequence(std::string& input, std::string testKey)
{
    // Reset insert if able to
    if (insert)
    {
        insertString = input;
    }

    bool test = !(testKey.empty());

    if (!kbhit() && !test) return; // no next char => bail

    char bracket = test ? testKey[0] : static_cast<char>(getchar());
    if (bracket != '[') return; // Not arrow or known seq

    if (!kbhit() && !test) return;

    char nextch = test ? testKey[1] : static_cast<char>(getchar());
    switch(nextch)
    {
        case 'A':
            // Up arrow
            navigateHistory(input, true);
            break;
        case 'B':
            // Down arrow
            navigateHistory(input, false);
            break;
        case 'C':
            // Right arrow
            moveCursorRight(1, &input);
            break;
        case 'D':
            // Left arrow
            if (cursorPos > 0)
            {
                moveCursorLeft(1);
            }
            break;
        case 'H':
            moveCursorToStart();
            break;
        case 'F':
            moveCursorToEnd(input);
            break;
        case 'O':
        {
            char lastChar = test ? (testKey.size() >= 3 ? testKey[2] : '\x00') : static_cast<char>(getchar());
            if (lastChar == 'H')
            {
                moveCursorToStart();
            }
            break;
        }
        case '1':
        {
            char nextchar = test ? ((testKey.size() >= 3) ? testKey[2] : '\x00') : static_cast<char>(getchar());
            if (nextchar == '~')
            {
                // Home
                moveCursorToStart();
            }
            else if (nextchar == ';')
            {
                nextchar = test ? ((testKey.size() >= 4) ? testKey[3] : '\x00') : static_cast<char>(getchar());
                if (nextchar == '5')
                {
                    nextchar = test ? ((testKey.size() >= 5) ? testKey[4] : '\x00') : static_cast<char>(getchar());
                    if (nextchar == 'C')
                    {
                        skipWordRight(input);
                    }
                    else if (nextchar == 'D')
                    {
                        skipWordLeft(input);
                    }
                }
            }
            else if (nextchar == 'H')
            {
                moveCursorToStart();
            }
            break;
        }
        case '4':
            if ((test ? ((testKey.size() >= 3) ? testKey[2] : '\x00') : static_cast<char>(getchar())) == '~')
            {
                // End
                moveCursorToEnd(input);
            }
            break;
        case '2':
            if ((test ? ((testKey.size() >= 3) ? testKey[2] : '\x00') : static_cast<char>(getchar())) == '~')
            {
                // Insert toggle
                insert = !insert;
                if (insert)
                {
                    insertString = input;
                }
                else
                {
                    insertString.clear();
                }
            }
            break;
        default:
            // Possibly more sequences
            break;
    }
}
    
void Console::navigateHistory(std::string& input, bool moveUp)
{
    if (history.empty()) return;

    std::string oldInput = input;

    // if moving up in history
    if (moveUp && historyIndex > 0)
    {
        --historyIndex;
    }
    // If moving down in history
    else if (!moveUp && historyIndex < history.size() - 1)
    {
        ++historyIndex;
    }
    // If at the end of history, disable browsing
    else if (!moveUp && historyIndex == history.size() - 1)
    {
        historyIndex++;
        browsingHistory = false;
        input = inputCache;
        updateDisplayInput(oldInput, input);
        return;
    }
    else
    {
        return;
    }

    // Update input with the selected history item
    input = history[historyIndex];
    browsingHistory = true;
    updateDisplayInput(oldInput, input);
}

void Console::updateDisplayInput(std::string& oldInput, std::string& input)
{
    // Calculate how many lines the input spans
    size_t oldLines = (oldInput.size() + initialLineLength) / terminalWidth + 1;

    // Move cursor to the start of the current line
    moveCursorToStart();

    // Save the starting position
    controller.saveCursorPosition();

    // Clear all lines occupied by the input
    for (size_t i = 0; i < oldLines; ++i)
    {
        controller.clearLineAfterCursor(); // Clear the current line
        if (i < oldLines)
        {
            controller.moveCursorDown(1); // Move down one line
            controller.moveCursorToStart();
        }
    }

    // Move back up to the starting position
    controller.restoreCursorPosition();

    // Write the updated input
    controller.print(input);
    cursorPos = input.size();
}

void Console::handlePrintableChar(char hInput, std::string& input)
{
    // Check for printable characters
    if (!std::isprint(static_cast<unsigned char>(hInput))) {
        return; // Not a printable character
    }

    // Reject specific control characters
    switch (hInput) {
        case '\n': // Newline
        case '\t': // Tab
        case '\b': // Backspace
        case '\r': // Carriage return
        case '\a': // Bell
        case '\v': // Vertical tab
        case '\f': // Form feed
        case '\033': // Escape character
            return; // Unsafe character
        default:
            break; // Continue checking
    }

    // If insert mode is on and not at the end => overwrite mid-line
    if (insert && cursorPos < input.size())
    {
        input[cursorPos] = hInput;
        cursorPos++;
        controller.print(std::string(1, hInput), Color::TERMINAL);
        rewriteTail(input, cursorPos);
    }
    else
    {
        // Append or insert at the cursor
        input.insert(cursorPos, 1, hInput);
        cursorPos++;
        controller.print(std::string(1, hInput), Color::TERMINAL);
        rewriteTail(input, cursorPos);
    }
}

std::string Console::getHistory(bool& his) 
{
    // Check if we need to retrieve the previous command
    if (his) 
    {
        // If not at the beginning of history, move to the previous command
        if (historyIndex != 0) 
        {
            historyIndex--;
            return history[historyIndex]; 
        }
    }
    // Check if we need to retrieve the next command
    else
    {
        // If not at the end of history, move to the next command
        if (historyIndex != history.size() - 1) 
        {
            historyIndex++; 
            return history[historyIndex];
        }
    }
    return history[historyIndex];
}

void Console::clearCurrentLine(std::string& input, std::string& nextConsoleLine) 
{
    moveCursorToStart();
    controller.clearLineAfterCursor();
    input = nextConsoleLine;
    cursorPos = input.length();
}
}
