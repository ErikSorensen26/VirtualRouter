#include <Console.h>


// Constructor for Console class
Console::Console() : Configs() 
{
    // Initialize history with an empty string
    history.push_back("");
    int busahd = 0;

    // Clear the console screen on Linux
#ifdef __linux__
    system("clear");
#else
#endif
}

// Clears the line after the current cursor position
void Console::clearLineAfterCursor() 
{
    for (int ch = 0; maxCommandLength < ch; ch++) 
    {
        cout << " "; 
    }
}

// Retrieves the cursor position on Windows
#ifdef _WIN32
COORD Console::getCursorPosition() 
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    // Get the console screen buffer information
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    COORD pos = csbi.dwCursorPosition; 
    return pos;
}
#else
// Retrieves the cursor position on Unix-like systems
CursorPosition Console::getCursorPosition() {
    CursorPosition pos;
    termios orig_termios;
    // Save the original terminal attributes
    tcgetattr(STDIN_FILENO, &orig_termios);
    termios raw = orig_termios;
    // Disable echo and canonical mode
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
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
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    if (buf[0] == '\033' && buf[1] == '[') {
        sscanf(buf, "\033[%d;%dR", &pos.row, &pos.col);
    } else {
        pos.row = -1;
        pos.col = -1;
    }
    return pos;
}

// Checks if a key has been pressed
int Console::kbhit() {
    struct termios oldt, newt;
    int ch;
    int oldf;

    // Get and modify terminal attributes for non-blocking input
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); 
    fcntl(STDIN_FILENO, F_SETFL, oldf); 

    if(ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }

    return 0;
}
#endif

int Console::getTerminalWidth() 
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    // Get the console screen buffer information
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    int columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return columns;
#else
    struct winsize w;
    // Get the terminal window size
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
#endif
}

// Moves the cursor left by the specified number of steps
void Console::moveCursorLeft(int steps) 
{
#ifdef _WIN32
    if (getCursorPosition().X == 0) 
    {
        // If at the left edge, move up and then right
        moveCursorUp(1);
        moveCursorRight(getTerminalWidth());
    } 
    else 
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        // Get the console screen buffer information
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        COORD pos = csbi.dwCursorPosition;
        pos.X = max(pos.X - steps, csbi.srWindow.Left); 
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
    }
#else
    if (getCursorPosition().col == 1) 
    {
        // If at the left edge, move up and then right
        moveCursorUp(1);
        moveCursorRight(getTerminalWidth() - 1);
    } 
    else 
    {
        std::cout << "\033[" << steps << "D";
    }
#endif
}

// Moves the cursor right by the specified number of steps
void Console::moveCursorRight(int steps) {
#ifdef _WIN32
    if (getCursorPosition().X == getTerminalWidth() - 1)
    {
        // If at the right edge, move left and then down
        moveCursorLeft(getTerminalWidth() - 1);
        moveCursorDown(1);
    } 
    else 
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        // Get the console screen buffer information
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        COORD pos = csbi.dwCursorPosition;
        pos.X = min(pos.X + steps, csbi.srWindow.Right);
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
    }
#else
    if (getCursorPosition().col == getTerminalWidth()) 
    {
        // If at the right edge, move left and then down
        moveCursorLeft(getTerminalWidth());
        moveCursorDown(1);
    } 
    else 
    {
        std::cout << "\033[" << steps << "C";
    }
#endif
}

// Moves the cursor up by the specified number of steps
void Console::moveCursorUp(int steps) 
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    // Get the console screen buffer information
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    COORD pos = csbi.dwCursorPosition;
    pos.Y = max(pos.Y - steps, csbi.srWindow.Top); 
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
#else
    if (steps > 0) 
    {
        std::cout << "\033[" << steps << "A"; 
    }
#endif
}

// Moves the cursor down by the specified number of steps
void Console::moveCursorDown(int steps) 
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    // Get the console screen buffer information
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    COORD pos = csbi.dwCursorPosition;
    pos.Y = min(pos.Y + steps, csbi.srWindow.Bottom);
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
#else
    if (steps > 0) 
    {
        std::cout << "\033[" << steps << "B";
    }
#endif
}

std::string Console::input()
{
#ifdef _WIN32
    // Save the current cursor position
    startPos = getCursorPosition();

    // Get the handle to the standard input
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    
    // Get the current console mode
    GetConsoleMode(hInput, &mode);
    // Set the console mode to disable processed input
    SetConsoleMode(hInput, mode & (~ENABLE_PROCESSED_INPUT));

    // Initialize the input string
    string input;

    DWORD read;
    INPUT_RECORD ir;
    DWORD written;

    // Process the next line input by removing the last character
    nextLine = nextLine.substr(0, nextLine.size() - 1);

    // Output the characters from nextLine into the console
    for (char in : nextLine) 
    {
        if (isprint(in))
         {
            input.insert(cursorPos, 1, in);
            ++cursorPos;
            cout << in; 
            cout << input.substr(cursorPos);
            moveCursorLeft(input.length() - cursorPos); 
        }
    }

    // Clear nextLine for future input
    nextLine = "";

    while (true) 
    {
        // Determine the maximum length of the command based on terminal width
        maxCommandLength = getTerminalWidth() - initialLineLength;

        // Read console input
        ReadConsoleInput(hInput, &ir, 1, &read);

        // Check if the event is a key event and the key is pressed
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) 
        {

            // Handle special case for the '?' character
            if (ir.Event.KeyEvent.uChar.AsciiChar == '?') 
            {
                cout << "?";
                return input + "?";
            }

            // Handle control key states for cursor movement
            if ((ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0) 
            {
                int pos = cursorPos;
                switch (ir.Event.KeyEvent.wVirtualKeyCode) {
                    case VK_LEFT:
                        if (cursorPos > 0) {
                            for (int ch = (input.substr(0, pos)).size() - 1; ch >= 0; ch--) 
                            {
                                if (input[ch] != ' ') {
                                    --cursorPos; 
                                    moveCursorLeft(1);
                                } else {
                                    break;
                                }
                            }
                        }
                        break;
                    case VK_RIGHT:
                        if (cursorPos < input.length()) 
                        {
                            for (char ch : input.substr(pos)) 
                            {
                                if (ch != ' ') {
                                    ++cursorPos;
                                    moveCursorRight(1);
                                } 
                                else 
                                {
                                    break;
                                }
                            }
                        }
                        break;
                }
            }

            // Handle various key events
            switch (ir.Event.KeyEvent.wVirtualKeyCode) {
                case VK_TAB:
                    {   
                        return input + "\t";
                    }
                    break;
                case VK_RETURN:
                    {   
                        // Add input to history if not empty and not last command
                        if (input != lastCommand && input != "") 
                        {
                            history.insert(history.end() - 1, input);
                        }
                        historyIndex = history.size() - 1;
                        lastCommand = input;
                        return input; 
                    }
                    break;
                case VK_BACK: 
                    if (cursorPos > 0) 
                    {
                        moveCursorLeft(1);
                        cursorPos--;
                        input.erase(cursorPos, 1);
                        int line = getCursorPosition().Y;
                        int siz = input.substr(cursorPos).size();
                        std::cout << input.substr(cursorPos) << " ";
                        // Move cursor left to correct position
                        while ((cursorPos + initialLineLength) % getTerminalWidth() != getCursorPosition().X || getCursorPosition().Y != line) 
                        {
                            moveCursorLeft(1);
                        }
                    }
                    break;
                case VK_DELETE:
                    if (cursorPos >= 0 && cursorPos < input.size()) 
                    {
                        input.erase(cursorPos, 1);
                        int siz = input.substr(cursorPos).size();
                        int line = getCursorPosition().Y; 
                        std::cout << input.substr(cursorPos) << " ";
                        // Move cursor left to correct position
                        while ((cursorPos + initialLineLength) % getTerminalWidth() != getCursorPosition().X || getCursorPosition().Y != line) 
                        {
                            moveCursorLeft(1);
                        }
                    }
                    break;
                case VK_LEFT: 
                    if (cursorPos > 0) 
                    {
                        --cursorPos; 
                        moveCursorLeft(1);
                    }
                    break;
                case VK_RIGHT:
                    if (cursorPos < input.length()) 
                    {
                        ++cursorPos;
                        moveCursorRight(1);
                    }
                    break;
                case VK_UP: 
                    {
                        if (historyIndex != 0)
                        {
                            bool his = true;
                            nextLine = getHistory(his); 
                            clearCurrentLine(input, nextLine);
                            input = nextLine;
                            nextLine = "";
                        }
                    }
                    break;
                case VK_DOWN: 
                    {
                        bool his = false;
                        nextLine = getHistory(his);
                        clearCurrentLine(input, nextLine);
                        input = nextLine;
                        nextLine = "";
                    }
                    break;
                case VK_INSERT: 
                    // Toggle insert mode
                    if (insert) 
                    {
                        insert = false;
                    } 
                    else 
                    {
                        insert = true;
                    }
                    break;
                default:
                    int pos = getCursorPosition().X;
                    bool isEnd = false;
                    // Check if cursor is at the end of the line
                    if (getCursorPosition().X == getTerminalWidth() - 1) 
                    {
                        isEnd = true;
                    }
                    if (insert && cursorPos != input.size()) 
                    {
                        cout << ir.Event.KeyEvent.uChar.AsciiChar;
                        cursorPos++;
                        input[cursorPos] = ir.Event.KeyEvent.uChar.AsciiChar;
                    } 
                    else if (ir.Event.KeyEvent.uChar.AsciiChar) 
                    {
                        input.insert(cursorPos, 1, ir.Event.KeyEvent.uChar.AsciiChar);
                        ++cursorPos;
                        cout << ir.Event.KeyEvent.uChar.AsciiChar;
                        string str1 = input.substr(cursorPos);
                        printString(str1);
                        if (isEnd && cursorPos - input.length() == 0) 
                        {
                            // No additional processing needed
                        } 
                        else 
                        {
                            for (int num = 0; num < input.length() - cursorPos; num++) 
                            {
                                moveCursorLeft(1);
                            }
                        }
                        if (getCursorPosition().X == pos) 
                        {
                            moveCursorRight(1);
                        }
                    }
                    break;
            }
        }
    }

    // Restore the original console mode
    SetConsoleMode(hInput, mode);
    return input;
#else
    // Save the current cursor position
    startPos = getCursorPosition();
    
    char hInput;
    bool ctrlPressed = false; 

    int cursorPos = 0; 
    string input;

    // Process the next line input by removing the last character
    nextLine = nextLine.substr(0, nextLine.size() - 1);

    // Output the characters from nextLine into the console
    for (char in : nextLine) 
    {
        if (isprint(in)) 
        {
            input.insert(cursorPos, 1, in);
            ++cursorPos;
            cout << in;
            cout << input.substr(cursorPos); 
            moveCursorLeft(input.length() - cursorPos - 1); 
        }
    }

    // Clear nextLine for future input
    nextLine = "";
    
    while (true) 
    {
        // Determine the maximum length of the command based on terminal width
        maxCommandLength = getTerminalWidth() - initialLineLength;

        // Check if a key has been pressed
        if (kbhit()) 
        {
            hInput = getchar();
            // Handle special character cases
            switch (hInput) {
                case '\x3f':
                    {
                        cout << "?";
                        return input + "?";
                    }
                case '\x09':
                    {
                        return input + "\t";
                    }
                case '\x0a':
                    {
                        // Add input to history if not empty and not last command
                        if (input != lastCommand && input != "") 
                        {
                            history.insert(history.end() - 1, input);
                        }
                        historyIndex = history.size() - 1;
                        lastCommand = input;
                        return input;
                    }
                case '\x7f':
                    if (cursorPos > 0) 
                    {
                        cursorPos--;
                        moveCursorLeft(1);
                        input.erase(cursorPos, 1);
                        int line = getCursorPosition().row;
                        int siz = input.substr(cursorPos).size();
                        cout << "\33[s"; 
                        cout << input.substr(cursorPos) << " "; 
                        cout << "\33[u";
                    }
                    break;
                case '\x7e':
                    if (cursorPos <= 0 && cursorPos < input.size()) 
                    {
                        input.erase(cursorPos, 1);
                        int siz = input.substr(cursorPos).size();
                        int line = getCursorPosition().row;
                        cout << "\33[s";
                        cout << input.substr(cursorPos) << " ";
                        cout << "\33[u";
                    }
                    break;
                case '\x1b':
                    {
                        if (getchar() == '[')
                        {
                            char nextch = getchar();
                            if (nextch == 'C') 
                            {
                                if (cursorPos < input.length()) 
                                {
                                    ++cursorPos;
                                    moveCursorRight(1); 
                                }
                            } else if (nextch == 'D') 
                            {
                                if (cursorPos > 0) 
                                {
                                    --cursorPos;
                                    moveCursorLeft(1);
                                }
                            } 
                            else if (nextch == 'A') 
                            {
                                if (historyIndex != 0) 
                                {
                                    bool his = true;
                                    nextLine = getHistory(his); 
                                    clearCurrentLine(input, nextLine);
                                    input = nextLine;
                                    nextLine = "";
                                }
                            }
                            else if (nextch == 'B') 
                            { 
                                bool his = false;
                                nextLine = getHistory(his);
                                clearCurrentLine(input, nextLine); 
                                input = nextLine;
                                nextLine = "";
                            } 
                            else if (nextch == '1') 
                            { 
                                int pos = cursorPos;
                                nextch = getchar(); 
                                if (nextch == ';') 
                                {
                                    nextch = getchar(); 
                                    if (nextch == '5') 
                                    {
                                        nextch = getchar(); 
                                        if (nextch == 'C') 
                                        {
                                            if (cursorPos < input.length()) 
                                            {
                                                for (char ch : input.substr(pos))
                                                {
                                                    if (ch != ' ') 
                                                    {
                                                        ++cursorPos; 
                                                        moveCursorRight(1); 
                                                    } 
                                                    else 
                                                    {
                                                        break;
                                                    }
                                                }
                                            }
                                        } 
                                        else if (nextch == 'D')
                                        {
                                            if (cursorPos < input.length()) 
                                            {
                                                for (char ch : input.substr(pos)) 
                                                {
                                                    if (ch != ' ') 
                                                    {
                                                        --cursorPos;
                                                        moveCursorLeft(1);
                                                    } 
                                                    else 
                                                    {
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            } 
                            else if (nextch == '2') 
                            { 
                                if (insert) 
                                {
                                    insert = false; 
                                }
                                else 
                                {
                                    insert = true; 
                                }
                            }
                        }
                    }
                    break;
                default:
                    if (isprint(hInput)) 
                    {
                        int pos = getCursorPosition().col;
                        bool isEnd = false;
                        if (getCursorPosition().col == getTerminalWidth()) 
                        {
                            isEnd = true;
                        }
                        if (insert && cursorPos != input.size())
                        {
                            cout << hInput;
                            cursorPos++; 
                            input[cursorPos] = hInput; 
                        } 
                        else if (isprint(hInput)) 
                        {
                            input.insert(cursorPos, 1, hInput); 
                            ++cursorPos;
                            cout << hInput;
                            string str1 = input.substr(cursorPos);
                            cout << "\33[s";
                            printString(str1);
                            cout << "\33[u";
                            if (isEnd) 
                            {
                                moveCursorRight(1);
                            }
                        }
                    }
                    break;
            }
            cout.flush();
        }
    }
    return input; 
#endif
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
    if (!his) 
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

void Console::clearCurrentLine(string& input, string& nextLine) 
{
#ifdef _WIN32
    // For Windows systems

    // Move cursor to the end of the current line
    while (cursorPos != input.size()) 
    {
        moveCursorRight(1);
        cursorPos++;
    }

    // Clear the line by moving cursor to the start and overwriting with spaces
    while (startPos.X != getCursorPosition().X || startPos.Y != getCursorPosition().Y) 
    {
        if (getCursorPosition().X != 0) 
        {
            moveCursorLeft(1); 
            cout << " "; 
            moveCursorLeft(1); 
        } 
        else 
        {
            moveCursorLeft(1); 
            cout << " "; 
        }
    }
    cursorPos = 0; 

    // Print the new line content
    for (char ch : nextLine) 
    {
        cout << ch;
        cursorPos++; 
    }
#else
    // For Unix-like systems

    // Move cursor to the end of the current line
    while (cursorPos != input.size()) 
    {
        moveCursorRight(1); 
        cursorPos++; 
    }

    // Clear the line by moving cursor to the start and overwriting with spaces
    while (startPos.col != getCursorPosition().col || startPos.row != getCursorPosition().row) 
    {
        if (getCursorPosition().row != 0) {
            moveCursorLeft(1); 
            cout << " "; 
            moveCursorLeft(1); 
        } 
        else 
        {
            moveCursorLeft(1); 
            cout << " "; 
        }
    }
    cursorPos = 0; // Reset cursor position to start

    // Print the new line content
    for (char ch : nextLine) 
    {
        cout << ch; 
        cursorPos++; 
    }
#endif
}

void Console::printString(string& string) 
{
    // Print each character in the provided string
    for (char ch : string) 
    {
        cout << ch; 
    }
}
