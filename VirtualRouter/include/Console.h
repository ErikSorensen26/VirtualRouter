#pragma once

#include "Configs.h" // Include the Configs class header for configuration handling

// Structure to hold cursor position with row and column
struct CursorPosition {
    int row, col;
};

// Console class inheriting from Configs for handling console input and output
class Console : public Configs {
public:
    // Constructor for initializing the Console object
    Console();
    void initConsole();

    // Function to read input from the console
    std::string input();

    // Flags and settings for the console
    bool insert = false; // Mode for inserting text
    size_t cursorPos = false; // Position of the cursor (initially false, likely a placeholder)

    // Line length settings
    int initialLineLength; // Length of the initial line
    int maxCommandLength; // Maximum length of a command

    // Strings for managing command input and history
    std::string nextLine; // Holds the next line of input
    std::string lastCommand; // Holds the last command executed

    // History management
    int historyIndex = 0; // Index for navigating through command history
    std::vector<std::string> history{}; // Vector to store command history

    // Cursor movement functions
    void moveCursorLeft(int steps); // Move cursor left by a number of steps
    void moveCursorRight(int steps); // Move cursor right by a number of steps
    void moveCursorUp(int steps); // Move cursor up by a number of steps
    void moveCursorDown(int steps); // Move cursor down by a number of steps

    // Functions for managing and retrieving command history
    std::string getHistory(bool& his); // Retrieve command history based on a flag
    void clearCurrentLine(std::string& input, std::string& nextLine); // Clear the current line of input

    // Terminal utility functions
    int getTerminalWidth(); // Get the width of the terminal
    void clearLineAfterCursor(); // Clear the line content after the cursor position

#ifdef _WIN32
    // Windows-specific functions
    COORD getCursorPosition(); // Get the cursor position on Windows
    COORD startPos = getCursorPosition(); // Initial cursor position on Windows
#else
    // Non-Windows (Linux/Unix) specific functions
    CursorPosition getCursorPosition(); // Get the cursor position on Linux/Unix
    int kbhit(); // Check if a key has been hit on Linux/Unix
    CursorPosition startPos = getCursorPosition(); // Initial cursor position on Linux/Unix
#endif

private:
    // Utility function to print a string to the console
    void printString(std::string& str);

    // Options for autocompletion
    std::vector<std::string> autocompleteOptions {"end", "exit"}; // List of options for autocompletion
};
