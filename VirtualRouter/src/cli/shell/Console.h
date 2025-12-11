// Console.h


#ifndef CONSOLE_H
#define CONSOLE_H

#include <string>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <vector>

class ConsoleTest; ///< Forward declaration of ConsoleTest

/**
 * @enum Color
 * @brief Represents all available terminal colors.
 */
enum class Color
{
    BLACK,
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    WHITE,
    NONE,

    // Secret prompt option
    TERMINAL,
    PROMPT
};

/**
 * @struct CursorPosition
 * @brief Represents the cursor's position in terms of row and comumn.
 */
struct CursorPosition 
{
    int row = 0; ///< The row position of the cursor.
    int col = 0; ///< The column position of the cursor.
};

class IConsole
{
public:

    virtual ~IConsole() = default;

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

    // Moves the cursor to the start of the line
    virtual void moveCursorToStart() = 0;

    // Moves the cursor left by 'count' positions
    virtual void moveCursorLeft(size_t count = 1) = 0;

    // Moves the cursor right by 'count' positions
    virtual void moveCursorRight(size_t count = 1) = 0;

    // Moves the cursor up by 'count' positions
    virtual void moveCursorUp(size_t count = 1) = 0;

    // Moves the cursor down by 'count' positions
    virtual void moveCursorDown(size_t count = 1) = 0;

    // Prints a string to the terminal
    virtual void print(const std::string& str, Color color = Color::NONE) = 0;

    // Gets the current cursor position
    virtual CursorPosition getCursorPosition() = 0;

    // Gets the terminal width
    virtual size_t getTerminalWidth() = 0;

    // Beep sound
    virtual void beep() = 0;
};

class RealConsole : public IConsole
{
public:
    virtual ~RealConsole() override = default;

    void clearScreen() override 
    {
        print("\033[2J\033[H"); // ANSI escape to clear screen and move cursor to home
    }

    void enableLineWrapping() override 
    {
        print("\033[?7h"); // Enable line wrapping
    }

    void clearLineAfterCursor() override 
    {
        print("\033[K"); // Clear from cursor to end of line
    }

    void saveCursorPosition() override 
    {
        print("\033[s"); // Save cursor position
    }

    void restoreCursorPosition() override 
    {
        print("\033[u"); // Restore cursor position
    }
    void moveCursorToStart() override
    {
        print("\033[1G");
    }

    void moveCursorLeft(size_t count) override 
    {
        if (count > 0) {
            print("\033[" + std::to_string(count) + "D"); // Move cursor left
        }
    }

    void moveCursorRight(size_t count) override 
    {
        if (count > 0) {
            print("\033[" + std::to_string(count) + "C"); // Move cursor right
        }
    }

    void moveCursorUp(size_t count) override 
    {
        if (count > 0) {
            print("\033[" + std::to_string(count) + "A"); // Move cursor up
        }
    }

    void moveCursorDown(size_t count) override 
    {
        if (count > 0) {
            print("\033[" + std::to_string(count) + "B"); // Move cursor down
        }
    }

    void print(const std::string& str, Color color = Color::NONE) override 
    {
        switch (color)
        {
            case Color::BLACK:
                std::cout << "\033[1;30m" << str << "\033[0m";
                break;
            case Color::RED:
                std::cout << "\033[1;31m" << str << "\033[0m";
                break;
            case Color::GREEN:
                std::cout << "\033[1;32m" << str << "\033[0m";
                break;
            case Color::YELLOW:
                std::cout << "\033[1;33m" << str << "\033[0m";
                break;
            case Color::BLUE:
                std::cout << "\033[1;34m" << str << "\033[0m";
                break;
            case Color::MAGENTA:
                std::cout << "\033[1;35m" << str << "\033[0m";
                break;
            case Color::CYAN:
                std::cout << "\033[1;36m" << str << "\033[0m";
                break;
            case Color::WHITE:
                std::cout << "\033[1;37m" << str << "\033[0m";
                break;
            case Color::NONE:
                std::cout << str;
                break;
            default:
                std::cout << str;
        }
    }

    CursorPosition getCursorPosition() override 
    {
        CursorPosition pos{-1, -1};
        termios orig, raw;
        tcgetattr(STDIN_FILENO, &orig); // Save original state
        raw = orig;

        // Turn off canonical & echo
        raw.c_lflag &= static_cast<unsigned int>(~(ICANON | ECHO));
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

        // Ask terminal for position
        std::cout << "\033[6n";
        std::cout.flush();

        char buf[32];
        size_t i = 0;
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

    size_t getTerminalWidth() override
    {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        return w.ws_col > 0 ? w.ws_col : 80;
    }

    void beep() override 
    {
        std::cout << "\a"; // ASCII Bell character
    }
};

/**
 * @class Console
 * @brief Handles Console inputs and other operations
 *
 * The Console class inherits from the Configs class and provides functionalities
 * for managing user input, cursor movements, and command history within the terminal.
 */
class Console
{
private:
    friend class Internal_ConsoleTest;
public:

    /**
     * @brief Constructor for the Console class.
     *
     * Initializes the Console object of invoking the Configs constructor
     */
    Console();

    /**
     * @brief Constructor for the Console class.
     *
     * Initializes the Console object of invoking the Configs constructor
     *
     * @param term Terminal deciding whether it's simulated or not
     */
    explicit Console(IConsole* term);

    /**
     * @brief Destructor for the Console class
     *
     * Cleans up any resources used by the Console object.
     */
    ~Console();

    /**
     * @brief Initializes the console settings.
     *
     * Clears the screen, enables line wrapping, initializes the input buffer,
     * and displays the initial prompt.
     */
    void initConsole();

    /**
     * @brief Retrieves and processes user input from the console.
     *
     * This functions enteres the main loop, capturing and handling user keystrokes,
     * managing command history navigation, and processing special keys.
     *
     * @return std::string The processed input command entered by the user.
     */
    std::string input(std::string input = "", bool pagination = false);
    
    /**
     * @brief Retrieves a command from the history based on navigation direction.
     *
     * Navigates through the command history either upwards or downwards based on the flag.
     *
     * @param his A boolean flag indicating the direction of navigation.
     *            - 'true': Move up in history.
     *            - 'false': Move down in history.
     * @return std::string The command retreived from history.
     */
    std::string getHistory(bool& his);
    
    /**
     * @brief Clears the current input line and sets it to the next line.
     *
     * Moves the cursor to the start of the line, clears the line,
     * and updates the input and nextLine strings accordingly.
     *
     * @param input Reference to the current input string.
     * @param nextLine reference to the next line string to be displayed.
     */
    void clearCurrentLine(std::string& input, std::string& nextLine); // Clear the current line of input

    /**
     * @brief Clears the line content after the current cursor position.
     *
     * Overwrites the line with spaces from the current cursor position to the end
     * of the terminal width and moves the cursor back
     */
    void clearLineAfterCursor(); // Clear the line content after the cursor position

    /**
     * @brief Sets the prompt string for the console.
     *
     * Updates the prompt displayed on the user and resets the cursor position.
     *
     * @param newPrompt The new prompt string to be set.
     */
    void setPrompt(const std::string& newPrompt);

    /**
     * @brief Get the cursor positions
     *
     * @return int Reference to cursor position
     * @note This returns a reference, be carefull.
     */
    size_t& getInputCursorPosition() { return cursorPos; }

    /**
     * @brief Get the initial command length
     *
     * @return int Reference to cursor position
     * @note This returns a reference, be carefull.
     */
    size_t& getInitialLineLength() { return initialLineLength; }

    // Member variables for line wrapping and display.
    IConsole* iConsole;  ///< Terminal deciding whether its using a simulated terminal.

protected:

    /**
     * @brief Rewrites the tail of the input line starting from a specific position.
     *
     * Updates the display by rewriting characters from the start position to the end,
     * handling line wrapping as necessary.
     *
     * @param input The current input string.
     * @param startPos The starting position of the rewrite
     */
    void rewriteTail(const std::string& input, size_t startPos, bool backspace = false);

    /**
     * @brief Handles special key inputs such as Enter, Tab, Backspace, etc.
     *
     * Processes special keys and updates the input string accorsingly.
     *
     * @param hInput The character representing the special key pressed.
     * @param input Reference to the current input string to be modified.
     * @return std::string The updates input string if a terminating condition is met; otherwise, an empty string.
     */
    std::string handleSpecialKey(char hInput, std::string& input);

    /**
     * @brief Handles escape sequences for arrow keys and other special inputs.
     *
     * Processes escape sequences to preform actions like navigating command history
     * or moving the cursor in different directions.
     *
     * @param input Reference to the current input string.
     */
    void handleEscapeSequence(std::string& input, std::string testKey = "");

    /**
     * @brief Handles printable character inputs.
     *
     * Inserts printable characters into the input string at the current cursor position,
     * managing insert and overwrite modes.
     *
     * @param hInput The printable character entered by the user.
     * @param input Reference to the current input string to be modified.
     */
    void handlePrintableChar(char hInput, std::string& input);

    /**
     * @brief Retrieves the current cursor position on Unix-like systems.
     *
     * Utilizes ANSI escape codes and terminal settings to query the cursor's row and column.
     *
     * @return CursorPosition The current cursor position with row and column indices.
     */
    CursorPosition getCursorPosition();

    /**
     * @brief Checks if a key has been pressed (non-blocking)
     *
     * Uses terminal settings to preform a non-blocking check for any keypress.
     *
     * @return int Returns 1 if a key has been pressed; otherwise, 0.
     */
    bool kbhit();

    /**
     * @brief Moves the cursor to the start of the current line.
     *
     * If the cursor is not already at the start, it moves the cursor left to the beginning.
     */
    size_t getTerminalWidth();

    /**
     * @brief Moves the cursor left by a specified number of steps.
     *
     * Sends ANSI escape codes to move the cursor right on the terminal.
     *
     * @param steps the number of positions to move the cursor left.
     */
    void moveCursorLeft(size_t steps);

    /**
     * @brief Moves the cursor right by a specified number of steps.
     *
     * Sends ANSI escape codes to move the cursor right on the terminal.
     *
     * @param steps the number of positions to move the cursor right.
     */
    void moveCursorRight(size_t steps, std::string* input = nullptr);

    /**
     * @brief Moves the cursor up by a specified number of steps.
     *
     * Sends ANSI escape codes to move the cursor right on the terminal.
     *
     * @param steps the number of positions to move the cursor up.
     */
    void moveCursorUp(size_t steps);

    /**
     * @brief Moves the cursor down by a specified number of steps.
     *
     * Sends ANSI escape codes to move the cursor right on the terminal.
     *
     * @param steps the number of positions to move the cursor down.
     */
    void moveCursorDown(size_t steps);

    /**
     * @brief Moves the cursor to the start of the current line.
     *
     * If the cursor id not already at the start, it moves the cirsor left to the beginning.
     */
    void moveCursorToStart();

    /**
     * @brief Moves the cursor to the end of the currnet input.
     *
     * Calculates the remaining steps needed to reach the end based on the input size
     * and moves the cursor right accordingly.
     *
     * @param input Reference to the current input string.
     */
    void moveCursorToEnd(std::string& input);

    /**
     * @brief Skips a word to the left of the cursor.
     *
     * Moves the cursor left, skipping over spaces and non-space characters to navigate
     * word by word
     *
     * @param input Reference to the current input string.
     */
    void skipWordLeft(std::string& input);

    /**
     * @brief Skips a word to the right of the cursor.
     *
     * Moves the cursor right, skipping over spaces and non-space characters to navigate
     * word by word.
     *
     * @param input Reference to the current input string.
     */
    void skipWordRight(std::string& input);

    /**
     * @brief Checks if the cursor is at the end of the current line.
     *
     * Determines if the cursor position plus the initial line length modulo the terminal width
     * equals zero, indicating the end of the line.
     *
     * @return true If the cursor is at the end of the line; otherwise, false.
     */
    bool isCursorAtLineEnd();

    /**
     * @brief Updates the display with the selected command from history.
     *
     * Clears the current input display and replaces it with the selected historical command.
     *
     * @param oldInput The previous input string before history navigation.
     * @param input The new input string retreived from history.
     */
    void updateDisplayInput(std::string& oldInput, std::string& input);

    /**
     * @brief Navigates through the command history.
     *
     * Moves up or down in the command history based on the direction flag.
     *
     * @param input Reference to the current input string to be updates.
     * @param moveUp Boolean flag indication the direction of navigating.
     *               - 'true': Move up in history.
     *               - 'false': Move down in history.
     */
    void navigateHistory(std::string& input, bool moveUp);

    CursorPosition startPos;       ///< Starting cursor position.
    size_t cursorPos = 0;          ///< Logical cursor position within inputBuffer.
    size_t terminalWidth = 80;     ///< Current terminal width.
    size_t oldInputLength = 0;     ///< Previous input length to track changes.
    size_t initialLineLength = 0;  ///< Initial starting position for a command.
    size_t maxCommandLength = 0;   ///< Maximum command length based on terminal width.
    std::string prompt;         ///< The current prompt string.

    std::string nextLine;       ///< Holds the next line of input
    std::string lastCommand;    ///< Holds the last command executed

    bool resized = false;       ///< Flag indicating if the terminal has been resized.
    bool insert = false;        ///< Flag indicating if the terminal is in insert mode.
    std::string insertString;   ///< String used for keeping the original while in insert mode.

    // History management
    size_t historyIndex = 0;               ///< Index for navigating through command history
    std::vector<std::string> history{}; ///< Vector to store command history
    bool browsingHistory = false;       ///< Indicates whether history is being accessed
    std::string inputCache;             ///< Current command being edited while browsing history
    std::string inputCacheBuffer;       ///< Buffer to store the input cache for general use

    // Options for autocompletion
    std::vector<std::string> autocompleteOptions {"end", "exit"}; ///< List of options for autocompletion
};

#endif // CONSOLE_H
