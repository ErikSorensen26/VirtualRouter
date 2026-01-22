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

/** @enum Color @brief ANSI-compatible color selections for terminal output.
 *
 * Represents all standard colors supported by most VT100-compatible terminals.
 * Additional internal values (TERMINAL, PROMPT) are used for specialized prompt
 * rendering and should not be interpreted as ANSI colors.
 */
enum class Color
{
    BLACK,  ///< Black colored text.
    RED,    ///< Red colored text.
    GREEN,  ///< Green colored text.
    YELLOW, ///< Yellow colored text.
    BLUE,   ///< Blue colored text.
    MAGENTA,///< Magenta colored text.
    CYAN,   ///< Cyan colored text.
    WHITE,  ///< White colored text.
    NONE,   ///< No colored or default.

    // Secret prompt option
    TERMINAL, ///< Internal cheat option for front end terminal.
    PROMPT    ///< Internal cheat option for front end prompt.
};

/**
 * @struct CursorPosition
 * @brief Represents terminal cursor coordinates (1-indexed row and column).
 *
 * Used to query and track cursor state during editing operations.
 */
struct CursorPosition 
{
    int row = 0; ///< The row position of the cursor.
    int col = 0; ///< The column position of the cursor.
};

/**
 * @class IConsole
 * @brief Abstract terminal I/O interface used by the CLI subsystem.
 *
 * This interface decouples the CLI from real terminal behavior, enabling:
 *
 * ### Testing & Simulation
 * - Automated console tests (mocked I/O behavior).
 * - Headless execution without a real TTY.
 *
 * ### Responsibilities
 * - Provide cursor movement primitives.
 * - Perform formatted printing.
 * - Report cursor position and terminal dimensions.
 * - Handle optional terminal control functions (beep, wrap, etc.).
 *
 * Implementations must guarantee that no method throws exceptions and that all
 * behavior is consistent with VT100-style terminal semantics where applicable.
 */
class IConsole
{
public:

    virtual ~IConsole() = default;

    /**
     * Clears the terminal screen and moves the cursor to home
     */
    virtual void clearScreen() = 0;

    /**
     * Enable line wrapping
     */
    virtual void enableLineWrapping() = 0;

    /**
     * Clears from the cursor to the end of the line
     */
    virtual void clearLineAfterCursor() = 0;

    /**
     * Saves the current cursor position
     */
    virtual void saveCursorPosition() = 0;

    /**
     * Restores the cursor position
     */
    virtual void restoreCursorPosition() = 0;

    /**
     * Moves the cursor to the start of the line
     */
    virtual void moveCursorToStart() = 0;

    /**
     * Moves the cursor left by 'count' positions
     */
    virtual void moveCursorLeft(size_t count = 1) = 0;

    /**
     * Moves the cursor right by 'count' positions
     */
    virtual void moveCursorRight(size_t count = 1) = 0;

    /**
     * Moves the cursor up by 'count' positions
     */
    virtual void moveCursorUp(size_t count = 1) = 0;

    /**
     * Moves the cursor down by 'count' positions
     */
    virtual void moveCursorDown(size_t count = 1) = 0;

    /**
     * Prints a string to the terminal
     */
    virtual void print(const std::string& str, Color color = Color::NONE) = 0;

    /**
     * Gets the current cursor position
     */
    virtual CursorPosition getCursorPosition() = 0;

    /**
     * Gets the terminal width
     */
    virtual size_t getTerminalWidth() = 0;

    /**
     * Beep sound
     */
    virtual void beep() = 0;
};

/**
 * @class RealConsole
 * @brief Concrete TTY-backed terminal implementation.
 *
 * This class interacts directly with ANSI escape sequences and POSIX terminal
 * APIs (`termios`, `ioctl`). It provides the actual console control mechanisms
 * used during runtime on Unix-like systems.
 *
 * ### Memory & Ownership
 * * - Stateless aside from local scratch buffers.
 *
 * ### Concurrency Model
 * * - Not thread-safe; assumes exclusive CLI ownership.
 */
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
 * @brief High-level CLI input processor and terminal interaction manager.
 *
 * This class implements:
 *
 * ### Responsibilities
 * - User input collection and real-time editing.
 * - Cursor movement, wrapping, and redraw logic.
 * - Command history browsing and modification.
 * - Prompt rendering and terminal state initialization.
 * - Integration with an abstract terminal interface (`IConsole`).
 *
 * It provides the interactive shell behavior expected from a router CLI, including:
 *
 * ### Interactions
 * - Arrow-key navigation  
 * - Backspace and delete handling  
 * - Word-skipping (Ctrl+Left/Right style semantics)  
 * - Inline insertion/overwrite modes  
 * - Retrieval and restoration of historical commands  
 *
 * ### Memory & Ownership
 * - Owns no external resources.
 * - Holds a non-owning pointer to an `IConsole` implementation.
 *
 * ### Concurrency Model
 * - Not thread-safe; intended for exclusive CLI session contexts.
 */
class Console
{
public:
    friend class Internal_ConsoleTest;

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

    IConsole* iConsole;  ///< Terminal deciding whether its using a simulated terminal.

protected:

    bool kbhit(); ///< Non-blocking check for pending keypress.

    void rewriteTail(const std::string& input, size_t startPos, bool backspace = false); ///< Redraws tail of input.

    std::string handleSpecialKey(char hInput, std::string& input); ///< Processes Enter, Backspace, Tab, etc.
    void handleEscapeSequence(std::string& input, std::string testKey = ""); ///< Handles arrow keys and escape sequences.
    void handlePrintableChar(char hInput, std::string& input); ///< Inserts printable characters into buffer.

    CursorPosition getCursorPosition(); ///< Queries terminal cursor position.
    size_t getTerminalWidth(); ///< Retrieves terminal width.

    void moveCursorLeft(size_t steps); ///< Moves cursor left.
    void moveCursorRight(size_t steps, std::string* input = nullptr); ///< Moves cursor right.
    void moveCursorUp(size_t steps); ///< Moves cursor up.
    void moveCursorDown(size_t steps); ///< Moves cursor down.
    void moveCursorToStart(); ///< Moves cursor to start of line.
    void moveCursorToEnd(std::string& input); ///< Moves cursor to end of input.

    void skipWordLeft(std::string& input); ///< Moves cursor left by one word.
    void skipWordRight(std::string& input); ///< Moves cursor right by one word.

    bool isCursorAtLineEnd(); ///< Checks whether cursor is at wrapping boundary.

    void updateDisplayInput(std::string& oldInput, std::string& input); ///< Replaces displayed text with history entry.

    void navigateHistory(std::string& input, bool moveUp); ///< Navigates history buffer.

    // CURSOR & LENGTH

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

    // HISTORY MANAGEMENT

    size_t historyIndex = 0;               ///< Index for navigating through command history
    std::vector<std::string> history{}; ///< Vector to store command history
    bool browsingHistory = false;       ///< Indicates whether history is being accessed
    std::string inputCache;             ///< Current command being edited while browsing history
    std::string inputCacheBuffer;       ///< Buffer to store the input cache for general use

    // Options for autocompletion
    std::vector<std::string> autocompleteOptions {"end", "exit"}; ///< List of options for autocompletion
};

#endif // CONSOLE_H
