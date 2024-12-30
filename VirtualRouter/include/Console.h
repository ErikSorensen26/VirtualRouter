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
    ~Console();
    void initConsole();

    // Main input loop
    std::string input();
    
    // Functions for managing and retrieving command history
    std::string getHistory(bool& his); // Retrieve command history based on a flag

    // Clear line
    void clearCurrentLine(std::string& input, std::string& nextLine); // Clear the current line of input

    // Strings for managing command input and history
    std::string nextLine; // Holds the next line of input
    std::string lastCommand; // Holds the last command executed

    // Terminal utility functions
    void clearLineAfterCursor(); // Clear the line content after the cursor position
protected:
    // Utility functions for printing
    void printString(std::string& str);
    void printLineBulk(const std::string& line);
    void rewriteTail(const std::string& input, int startPos);

    // Key handling for input()
    std::string handleSpecialKey(char hInput, std::string& input);
    void handleEscapeSequence(std::string& input);
    void handlePrintableChar(char hInput, std::string& input);

    // Cursor and colsole utilities
    CursorPosition getCursorPosition();
    int kbhit();
    int getTerminalWidth();
    void moveCursorLeft(int steps);  // Move cursor left by a number of steps
    void moveCursorRight(int steps); // Move cursor right by a number of steps
    void moveCursorUp(int steps);    // Move cursor up by a number of steps
    void moveCursorDown(int steps);  // Move cursor down by a number of steps

    // Large cursor movement
    void moveCursorToStart();
    void moveCursorToEnd(std::string& input);
    void skipWordLeft(std::string& input);
    void skipWordRight(std::string& input);

    // Display Management
    void setPrompt(const std::string newPrompt);
    bool isCursorAtLineEnd();
    void updateDisplayInput(std::string& oldInput, std::string& input);

    // Member variables for line wrapping and display
    CursorPosition startPos;                // Starting cursor position
    int cursorPos = 0;                      // Logical cursor position within inputBuffer
    int terminalWidth = 80;                 // Current terminal width
    int oldInputLength = 0;                 // Previous input length to track changes

    int initialLineLength = 0;
    int maxCommandLength = 0;
    std::string prompt;

    bool resized = false;                   // Flag indicating if the terminal has been resized

    bool insert = false;

    // History management
    void navigateHistory(std::string& input, bool moveUp);
    int historyIndex = 0; // Index for navigating through command history
    std::vector<std::string> history{}; // Vector to store command history
    bool browsingHistory = false; // Indicates whether history is being accessed
    std::string inputCache; // Current command being edited while browsing history

    // Options for autocompletion
    std::vector<std::string> autocompleteOptions {"end", "exit"}; // List of options for autocompletion
};
