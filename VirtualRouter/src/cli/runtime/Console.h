/**
 * @file Console.h
 * @brief Console I/O: user input, cursor control, and command-line editing.
 *
 * Handles terminal input/output including character-by-character input processing,
 * history navigation, cursor positioning, and command-line editing features.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <string>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <vector>
#include <Mock.hpp>
#include "ConsoleController.hpp"

class ConsoleTest;

namespace cli
{

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
     *
     * @param term Terminal deciding whether it's simulated or not
     */
    explicit Console(ConsoleController& term);

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
    size_t getInputCursorPosition() { return cursorPos; }

    /**
     * @brief Get the initial command length
     *
     * @return int Reference to cursor position
     * @note This returns a reference, be carefull.
     */
    size_t getInitialLineLength() { return initialLineLength; }

    std::vector<std::string> getHistory() { return history; }

    // Member variables for line wrapping and display.
    ConsoleController& controller;  ///< Terminal deciding whether its using a simulated terminal.

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
}

#endif // CONSOLE_H
