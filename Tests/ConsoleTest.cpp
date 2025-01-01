#include <Console.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <Terminal.h>
#include <sstream>

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

// Test Fixture for Console
class ConsoleTest : public ::testing::Test
{
protected:
    Console* console;
    std::streambuf* originalCoutBuffer;
    std::ostringstream testCout;

    void SetUp() override
    {
        // Redirect std::cout to capture outputs for testing
        originalCoutBuffer = std::cout.rdbuf();
        std::cout.rdbuf(testCout.rdbuf());

        // Initialize the console object
        console = new Console();
    }

    void TearDown() override
    {
        delete console;
        // Restore original std::cout buffer
        std::cout.rdbuf(originalCoutBuffer);
    }

    int getTerminalWidth() {return console->getTerminalWidth();}
    void setCursorPos(int pos) {console->cursorPos = pos;}
    int getCursorPos() {return console->cursorPos;}
    void moveCursorLeft(int siz) {console->moveCursorLeft(siz);}
};

// Test setting the prompt
TEST_F(ConsoleTest, SetPrompt_ShouldUpdatePromptCorrectly)
{
    std::string newPrompt = "TestPrompt>";
    console->setPrompt(newPrompt);
    EXPECT_EQ(testCout.str(), newPrompt);
}

// Test initializing the console
TEST_F(ConsoleTest, InitConsole_ShouldClearScreenAndSetPrompt)
{
    std::string prompt = "InitPrompt>";
    console->setPrompt(prompt);
    testCout.str(""); // Clear previous output

    console->initConsole();

    std::string expectedOutput = "\033[2J\033[H" "\033[?7h" + prompt;
    EXPECT_EQ(testCout.str(), expectedOutput);
}

// Test getting terminal width
TEST_F(ConsoleTest, GetTerminalWidth_ShouldReturnCorrectWidth)
{
    int width = getTerminalWidth();
    EXPECT_EQ(width, 80);
}

// Test moving cursor left
TEST_F(ConsoleTest, MoveCursorLeft_ShouldMoveCursorLeftProperly)
{
    setCursorPos(5);
    int initialLineLength = 10;
    int terminalWidth = 80;

    // Expected to move left by 1
    moveCursorLeft(1);

    EXPECT_EQ(getCursorPos(), 4);

}
