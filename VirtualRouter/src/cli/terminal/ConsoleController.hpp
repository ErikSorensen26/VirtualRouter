/**
 * @file ConsoleController.hpp
 * @brief Console control interface: terminal output, colors, and cursor manipulation.
 * @ingroup CLI_RUNTIME
 *
 * Provides an abstraction for console-level operations including text output,
 * color/styling, cursor positioning, and terminal feature detection.
 * Can be mocked for testing.
 */

#ifndef CONSOLE_CONTROLLER_HPP
#define CONSOLE_CONTROLLER_HPP

#include <string>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <Mock.hpp>

#ifndef ENABLE_CUSTOM_CONSOLE
#define ENABLE_CUSTOM_CONSOLE 1
#endif

#if defined(ENABLE_MOCK) || ENABLE_CUSTOM_CONFIG
    #define CONSOLE_MOCK virtual
#else
    #define CONSOLE_MOCK
#endif

namespace cli
{
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
class ConsoleController
{
public:
    CONSOLE_MOCK ~ConsoleController() = default;

    CONSOLE_MOCK void clearScreen()
    {
        print("\033[2J\033[H"); // ANSI escape to clear screen and move cursor to home
    }

    CONSOLE_MOCK void enableLineWrapping()
    {
        print("\033[?7h"); // Enable line wrapping
    }

    CONSOLE_MOCK void clearLineAfterCursor()
    {
        print("\033[K"); // Clear from cursor to end of line
    }

    CONSOLE_MOCK void saveCursorPosition()
    {
        print("\033[s"); // Save cursor position
    }

    CONSOLE_MOCK void restoreCursorPosition()
    {
        print("\033[u"); // Restore cursor position
    }
    CONSOLE_MOCK void moveCursorToStart()
    {
        print("\033[1G");
    }

    CONSOLE_MOCK void moveCursorLeft(size_t count)
    {
        if (count > 0) {
            print("\033[" + std::to_string(count) + "D"); // Move cursor left
        }
    }

    CONSOLE_MOCK void moveCursorRight(size_t count)
    {
        if (count > 0) {
            print("\033[" + std::to_string(count) + "C"); // Move cursor right
        }
    }

    CONSOLE_MOCK void print(const std::string& str, Color color = Color::NONE)
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

    CONSOLE_MOCK void flush()
    {
        std::cout.flush();
    }

    CONSOLE_MOCK CursorPosition getCursorPosition()
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
                break;
            if (buf[i] == 'R')
                break;
            i++;
        }
        buf[i] = '\0';

        // Restore terminal
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);

        // Parse row, col from e.g. "/033[12;40R"
        if (buf[0] == '\033' && buf[1] == '[')
            std::sscanf(buf, "\033[%d;%dR", &pos.row, &pos.col);
        return pos;
    }

    CONSOLE_MOCK size_t getTerminalWidth()
    {
        if (terminalWidthOverride > 0) return terminalWidthOverride;
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        return w.ws_col > 0 ? w.ws_col : 80;
    }

    void setTerminalWidth(size_t w)
    {
        terminalWidthOverride = w;
    }

private:
    size_t terminalWidthOverride = 0;

    CONSOLE_MOCK void beep()
    {
        std::cout << "\a"; // ASCII Bell character
    }
};
}

#endif
