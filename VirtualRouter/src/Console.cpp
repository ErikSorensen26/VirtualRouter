#include <Console.h>
#include <iostream>
#include <cstdio>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <vector>

// Constructor for Console class
Console::Console() : Configs() {}

// Destructor to clean up
Console::~Console() {}

void Console::setPrompt(const std::string newPrompt)
{
    prompt = newPrompt;
    cursorPos = 0;
    initialLineLength = prompt.length();

    // Clear current input on the screen and pring new prompt
    std::cout << prompt;
    std::cout.flush();
}

bool Console::isCursorAtLineEnd()
{
    int terminalWidth = getTerminalWidth();
    return (cursorPos + initialLineLength) % terminalWidth == 0;
}

// Initialization
void Console::initConsole()
{
    // Clear the screen once
    std::cout << "\033[2J\033[H"; // ANSI escape to clear and move cursor to top
    std::cout << "\033[?7h"; // Enable line wrapping

    // Initialize inputBuffer as empty
    cursorPos = 0;

    // Print the prompt
    std::cout << prompt;
    std::cout.flush();
}

// Clears the line after the current cursor position
void Console::clearLineAfterCursor() 
{
    int width = getTerminalWidth();
    std::cout << std::string(width, ' ');
    moveCursorLeft(width);
}

// Retrieves the cursor position on Unix-like systems
CursorPosition Console::getCursorPosition() {
    CursorPosition pos{-1, -1};
    termios orig, raw;
    tcgetattr(STDIN_FILENO, &orig); // Save original state
    raw = orig;

    // Turn off canonical & echo
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    // Ask terminal for position
    std::cout << "\033[6n";
    std::cout.flush();

    char buf[32];
    unsigned int i = 0;
    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, buf + i, 1) != 1)
        {
            break;
        }
        if (buf[i] == 'R')
        {
            break;
        }
        i++;
    }
    buf[i] = '\0';

    // Restore terminal
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);

    // Parse row, col from e.g. "/033[12;40R"
    if (buf[0] == '\033' && buf[1] == '[')
    {
        std::sscanf(buf, "\033[%d;%dR", &pos.row, &pos.col);
    }
    return pos;
}

// Checks if a key has been pressed
int Console::kbhit() 
{
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
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

int Console::getTerminalWidth() 
{
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col > 0 ? w.ws_col : 80;
}

// Moves the cursor left by the specified number of steps
void Console::moveCursorLeft(int steps) 
{
    for (int i = 0; i < steps; ++i)
    {
        if (cursorPos > 0)
        {
            if ((cursorPos + initialLineLength) % getTerminalWidth() == 0)
            {
                // Move to the previous line
                std::cout << "\033[A";
                std::cout << "\033[" << terminalWidth << "C";
            }
            else
            {
                // Move left
                std::cout << "\033[D";
            }
        }
        cursorPos--;
    }
}

// Moves the cursor right by the specified number of steps
void Console::moveCursorRight(int steps) 
{
    int terminalWidth = getTerminalWidth();
    for (int i = 0; i < steps; ++i)
    {
        if ((cursorPos + initialLineLength) % terminalWidth == terminalWidth - 1)
        {
            // Move to the next line
            std::cout << "\033[B\033[1G";
        }
        else
        {
            // Move right
            std::cout << "\033[C";
        }
        cursorPos++;
    }
}

// Moves the cursor up by the specified number of steps
void Console::moveCursorUp(int steps) 
{
    if (steps > 0) 
    {
        std::cout << "\033[" << steps << "A"; 
    }
}

// Moves the cursor down by the specified number of steps
void Console::moveCursorDown(int steps) 
{
    // Move cursor left 'cursorPos' times to reach the beginning
    if (steps > 0) 
    {
        std::cout << "\033[" << steps << "B";
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
    if ((int)input.size() > cursorPos)
    {
        int steps = input.size() - cursorPos;
        moveCursorRight(steps);
    }
}

void Console::skipWordLeft(std::string& input)
{
    if (cursorPos == 0) return;

    int newPos = cursorPos - 1;

    if (input[newPos - 1] == ' ')
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
    int steps = cursorPos - newPos;
    moveCursorLeft(steps);
}

void Console::skipWordRight(std::string& input)
{
    if (cursorPos >= (int)input.size()) return;

    int newPos = cursorPos;

    // Skip space if currently on one
    if (input[newPos] != ' ')
    {
        while (newPos < (int)input.size() && input[newPos] != ' ')
        {
            newPos++;
        }
    }
    // Skip any spaces if on them
    while (newPos < (int)input.size() && input[newPos] == ' ')
    {
        newPos++;
    }
    int steps = newPos - cursorPos;
    moveCursorRight(steps);
}

void Console::printLineBulk(const std::string& line)
{
    std::cout << line;
}

void Console::rewriteTail(const std::string& input, int startPos)
{
    int terminalWidth = getTerminalWidth();
    int currentColumn = (startPos + initialLineLength) % terminalWidth;

    // Save the current cursor position
    std::cout << "\033[s";

    // Rewrite the input from the start position
    for (size_t i = startPos; i < input.size(); ++i)
    {
        if (currentColumn >= terminalWidth)
        {
            std::cout << "\n";
            currentColumn = 0;
        }
        std::cout << input[i];
        ++currentColumn;
    }

    // Clear any leftover characters on the current line and subsequent lines
    int leftoverSpace = terminalWidth - currentColumn;
    if (leftoverSpace > 0)
    {
        std::cout << std::string(leftoverSpace, ' ');
    }

    // Clear leftover characters
    std::cout << " ";

    // Restore the cursor position
    std::cout << "\033[u";
}

std::string Console::input()
{
    // Save starting cursor position
    CursorPosition startPos = getCursorPosition();
    cursorPos = 0;

    // Startup Variables
    std::string input;

    // Print the nextLine if something is queued
    if (!nextLine.empty())
    {
        if (nextLine[nextLine.length() - 1] == ' ')
        {
            nextLine.pop_back();
        }
        input = nextLine;
        cursorPos = input.size();
        oldInputLength = input.size();
        std::cout << nextLine;
        nextLine.clear();
    }
    
    while (true) 
    {
        maxCommandLength = getTerminalWidth() - initialLineLength;

        // Non-blocking check for keypress
        if (kbhit()) 
        {
            char hInput = getchar();

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
    switch (hInput)
    {
        case '\x3f': // '?'
        {
            // Print '?' and return, so we exit the input loop
            std::cout << "?";
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
        case '\x7f': // Backspace
        {
            if (cursorPos > 0)
            {
                cursorPos--;

                if ((cursorPos + initialLineLength + 1) % getTerminalWidth() == 0)
                {
                    std::cout << "\033[A\033[" << getTerminalWidth() << "C";
                }
                else
                {
                    std::cout << "\b \b";
                }

                input.erase(cursorPos, 1);
                rewriteTail(input, cursorPos);
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

void Console::handleEscapeSequence(std::string& input)
{
    if (!kbhit()) return; // no next char => bail

    char bracket = getchar();
    if (bracket != '[') return; // Not arrow or known seq

    if (!kbhit()) return;

    char nextch = getchar();
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
            if (cursorPos < (int)input.size())
            {
                moveCursorRight(1);
            }
            break;
        case 'D':
            // Left arrow
            if (cursorPos > 0)
            {
                moveCursorLeft(1);
            }
            break;
        case '1':
        {
            char nextchar = getchar();
            if (nextchar == '~')
            {
                // Home
                moveCursorToStart();
            }
            else if (nextchar == ';')
            {
                nextchar = getchar();
                if (nextchar == '5')
                {
                    nextchar = getchar();
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
            break;
        }
        case '4':
            if (getchar() == '~')
            {
                // End
                moveCursorToEnd(input);
            }
            break;
        case '2':
            if (getchar() == '~')
            {
                // Insert toggle
                insert  = !insert;
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
    else if (!moveUp && historyIndex < (int)history.size() - 1)
    {
        ++historyIndex;
    }
    // If at the end of history, disable browsing
    else if (!moveUp && historyIndex == (int)history.size() - 1)
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
    int terminalWidth = getTerminalWidth();
    int oldLines = (oldInput.size() + initialLineLength) / terminalWidth + 1;
    int newLines = (input.size() + initialLineLength) / terminalWidth + 1;

    // Move cursor to the start of the current line
    moveCursorToStart();

    // Save the starting position
    std::cout << "\033[s";

    // Clear all lines occupied by the input
    for (int i = 0; i < oldLines; ++i)
    {
        std::cout << "\033[K"; // Clear the current lines
        if (i < oldLines)
        {
            std::cout << "\033[B"; // Move down one line
            std::cout << "\033[1G";
        }
    }

    // Move back up to the starting position
    std::cout << "\033[u";

    // Write the updated input
    std::cout << input;
    cursorPos = input.size();
}

void Console::handlePrintableChar(char hInput, std::string& input)
{
    // If insert mode is on and not at the end => insert mid-line
    if (insert && cursorPos < (int)input.size())
    {
        input.insert(cursorPos, 1, hInput);
        cursorPos++;
        std::cout << hInput;
        rewriteTail(input, cursorPos);
    }
    else
    {
        // Append or insert at the cursor
        input.insert(cursorPos, 1, hInput);
        cursorPos++;
        std::cout << hInput;
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

void Console::clearCurrentLine(std::string& input, std::string& nextLine) 
{
    moveCursorToStart();
    std::cout << "\033[K";
    input = nextLine;
    cursorPos = input.length();
}

void Console::printString(std::string& string) 
{
    std::cout << string;
}
