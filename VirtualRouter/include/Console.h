#pragma once

#include "Configs.h" // Include the Configs class header for configuration handling

/**
 * @struct CursorPosition
 * @brief Represents the cursor's position in terms of row and comumn.
 */
struct CursorPosition {
    int row; ///< The row position of the cursor.
    int col; ///< The column position of the cursor.
};

/**
 * @class Console
 * @brief Handles Console inputs and other operations
 *
 * The Console class inherits from the Configs class and provides functionalities
 * for managing user input, cursor movements, and command history within the terminal.
 */
class Console : public Configs {
public:
    friend class ConsoleTest;

    /**
     * @brief Constructor for the Console class.
     *
     * Initializes the Console object of invoking the Configs constructor
     */
    Console();

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
    std::string input();
    
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
    void setPrompt(const std::string newPrompt);

    /**
     * @brief Edit the cursor positions while testing.
     *
     * Get the cursor prompt to reset for testing purposes.
     *
     * @return int Reference to cursor position
     * @note This returns a reference, be carefull.
     */
    int& getInputCursorPosition() {return cursorPos;}

protected:

    /**
     * @brief Prints a string to the console.
     *
     * Outputs the provided string to the console withoug additional formatting.
     *
     * @param str The string to be printed.
     */
    void printString(std::string& str);

    /**
     * @brief Prints an entire line of text to the console in bulk.
     *
     * Outputs the provided line string directory to the console.
     *
     * @param line The line string to be printed.
     */
    void printLineBulk(const std::string& line);

    /**
     * @brief Rewrites the tail of the input line starting from a specific position.
     *
     * Updates the display by rewriting characters from the start position to the end,
     * handling line wrapping as necessary.
     *
     * @param input The current input string.
     * @param startPos The position in the input string from where to start rewriting.
     */
    void rewriteTail(const std::string& input, int startPos);

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
    void handleEscapeSequence(std::string& input);

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
    int kbhit();

    /**
     * @brief Moves the cursor to the start of the current line.
     *
     * If the cursor is not already at the start, it moves the cursor left to the beginning.
     */
    int getTerminalWidth();

    /**
     * @brief Moves the cursor left by a specified number of steps.
     *
     * Sends ANSI escape codes to move the cursor right on the terminal.
     *
     * @param steps the number of positions to move the cursor left.
     */
    void moveCursorLeft(int steps);

    /**
     * @brief Moves the cursor right by a specified number of steps.
     *
     * Sends ANSI escape codes to move the cursor right on the terminal.
     *
     * @param steps the number of positions to move the cursor right.
     */
    void moveCursorRight(int steps);

    /**
     * @brief Moves the cursor up by a specified number of steps.
     *
     * Sends ANSI escape codes to move the cursor right on the terminal.
     *
     * @param steps the number of positions to move the cursor up.
     */
    void moveCursorUp(int steps);

    /**
     * @brief Moves the cursor down by a specified number of steps.
     *
     * Sends ANSI escape codes to move the cursor right on the terminal.
     *
     * @param steps the number of positions to move the cursor down.
     */
    void moveCursorDown(int steps);

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

    // Member variables for line wrapping and display.
    CursorPosition startPos;    ///< Starting cursor position.
    int cursorPos = 0;          ///< Logical cursor position within inputBuffer.
    int terminalWidth = 80;     ///< Current terminal width.
    int oldInputLength = 0;     ///< Previous input length to track changes.
    int initialLineLength = 0;  ///< Initial starting position for a command.
    int maxCommandLength = 0;   ///< Maximum command length based on terminal width.
    std::string prompt;         ///< The current prompt string.

    std::string nextLine;       ///< Holds the next line of input
    std::string lastCommand;    ///< Holds the last command executed

    bool resized = false;       ///< Flag indicating if the terminal has been resized.
    bool insert = false;        ///< Flag indicating if the terminal is in insert mode.

    // History management
    int historyIndex = 0;               ///< Index for navigating through command history
    std::vector<std::string> history{}; ///< Vector to store command history
    bool browsingHistory = false;       ///< Indicates whether history is being accessed
    std::string inputCache;             ///< Current command being edited while browsing history

    // Options for autocompletion
    std::vector<std::string> autocompleteOptions {"end", "exit"}; ///< List of options for autocompletion
};
