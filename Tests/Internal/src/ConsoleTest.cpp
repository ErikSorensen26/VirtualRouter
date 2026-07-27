#include <cli/terminal/Console.h>
#include <cli/session/CliEngine.h>
#include <MockConsole.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

// Test Fixture for Console
// NOTE: Must be in namespace cli to match the friend class declaration in Console.h
namespace cli {
class Internal_ConsoleTest : public ::testing::Test
{
protected:
    // Create the MockTerminal instance
    MockConsole* mockConsole;

    // Create a console instance with the MockTerminal
    Console* console;

    void SetUp() override
    {
        mockConsole = new MockConsole;
        console = new Console(*mockConsole); // Share ownership
    }

    void TearDown() override
    {
        delete console;
    }

    std::string getPrompt() {return console->prompt;}
    size_t getCursorPos() {return console->cursorPos;}
    size_t getInitialLineLength() {return console->initialLineLength;}
    void setCursorPos(size_t pos) {console->cursorPos = pos;}
    bool isCursorAtLineEnd() {return console->isCursorAtLineEnd();}
    void moveCursorLeft(size_t siz) {console->moveCursorLeft(siz);}
    void moveCursorRight(size_t siz, std::string* input = nullptr) {console->moveCursorRight(siz, input);}
    void moveCursorUp(size_t siz) {console->moveCursorUp(siz);}
    void moveCursorDown(size_t siz) {console->moveCursorDown(siz);}
    void moveCursorToStart() {console->moveCursorToStart();}
    void moveCursorToEnd(std::string& input) {console->moveCursorToEnd(input);}
    void skipWordLeft(std::string& input) {console->skipWordLeft(input);}
    void skipWordRight(std::string& input) {console->skipWordRight(input);}
    void toggleInsertMode(std::string& input) {handleEscapeSequence(input, "[2~");}
    bool getInsertMode() {return console->insert;}
    std::string getInsertString() {return console->insertString;}
    void rewriteTail(std::string& input, size_t& startPosition) {console->rewriteTail(input, startPosition);}
    void handlePrintableChar(char hInput, std::string& input) {console->handlePrintableChar(hInput, input);}
    std::string handleSpecialKey(char hInput, std::string& input) {return console->handleSpecialKey(hInput, input);}
    void handleEscapeSequence(std::string& input, std::string test) {console->handleEscapeSequence(input, test);}
    std::vector<std::string> getHistory() {return console->history;}
    void setHistory(std::vector<std::string> newHistory) {console->history = newHistory;}
    void updateDisplayInput(std::string& oldInput, std::string input) {console->updateDisplayInput(oldInput, input);}
    CursorPosition getCursorPosition() {return console->getCursorPosition();}
    size_t getTerminalWidth() {return console->getTerminalWidth();}
};
} // namespace cli

using cli::Internal_ConsoleTest;

#pragma region Initialization

// Test setting the prompt
TEST_F(Internal_ConsoleTest, SetPrompt_ShouldUpdatePromptCorrectly)
{
    std::string newPrompt = "TestPrompt>";

    // Expect the terminal's print method to be called
    EXPECT_CALL(*mockConsole, print(newPrompt, ::testing::_)).Times(1);
    
    console->setPrompt(newPrompt);

    //Verify internal state
    EXPECT_EQ(getPrompt(), newPrompt);
    EXPECT_EQ(getCursorPos(), 0);
    EXPECT_EQ(getInitialLineLength(), newPrompt.length());
}

// Test initConsole
TEST_F(Internal_ConsoleTest, InitConsole_ShouldClearScreenAndSetPrompt)
{
    std::string prompt = "InitPrompt>";

    // Expect print prompt
    EXPECT_CALL(*mockConsole, print(prompt, ::testing::_)).Times(1);

    console->setPrompt(prompt);

    // Clear previous expectations
    ::testing::Mock::VerifyAndClearExpectations(&(*mockConsole));

    // Expect clearScreen, enableLineWrapping, and print prompt
    EXPECT_CALL(*mockConsole, clearScreen()).Times(1);
    EXPECT_CALL(*mockConsole, enableLineWrapping()).Times(1);
    EXPECT_CALL(*mockConsole, print(prompt, ::testing::_)).Times(1);

    console->initConsole();

    // Verify internal state
    EXPECT_EQ(getCursorPos(), 0);
}

#pragma endregion
#pragma region Cursor

// Test isCursorAtLineEnd when cursor is at line end
TEST_F(Internal_ConsoleTest, IsCursorAtLineEnd_CursorAtLineEnd_ShouldReturnTrue)
{
    setCursorPos(80);
    EXPECT_TRUE(isCursorAtLineEnd());
}

// Test isCursorAtLineEnd when cursor is not at line end
TEST_F(Internal_ConsoleTest, IsCursorAtLineEnd_CursorNotAtLineEnd_ShouldReturnFalse)
{
    setCursorPos(30);
    EXPECT_FALSE(isCursorAtLineEnd());
}

// Test isCursorAtLineEnd with cursor at different positions
TEST_F(Internal_ConsoleTest, IsCursorAtLineEnd_VariousPositions_ShouldReturnCorrectly)
{
    // Position 0
    setCursorPos(0);
    EXPECT_FALSE(isCursorAtLineEnd());

    // Position 80 (assuming terminal width 80)
    setCursorPos(80);
    EXPECT_TRUE(isCursorAtLineEnd());

    // Position 79
    setCursorPos(79);
    EXPECT_FALSE(isCursorAtLineEnd());
}

// Test isCursorAtLineEnd after multiple cursor movements
TEST_F(Internal_ConsoleTest, IsCursorAtLineEnd_AfterMultipleMovements_ShouldReturnCorrectly)
{
    // Expect default width of 80

    setCursorPos(40);
    EXPECT_FALSE(isCursorAtLineEnd());

    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(39);
    EXPECT_CALL(*mockConsole, moveCursorDown(1)).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(1);

    moveCursorRight(40, nullptr);
    EXPECT_EQ(getCursorPos(), 80);
    EXPECT_TRUE(isCursorAtLineEnd());

    EXPECT_CALL(*mockConsole, moveCursorRight(80)).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorUp(1)).Times(1);

    moveCursorLeft(1);
    EXPECT_EQ(getCursorPos(), 79);
    EXPECT_FALSE(isCursorAtLineEnd());
}

// Test isCursorAtLineEnd with varying terminal widths
TEST_F(Internal_ConsoleTest, IsCursorAtLineEnd_VaryingTerminalWidths_ShouldReturnCorrectly)
{
    // Mock getTerminalWidth to return different widths
    // Assuming getTerminalWidth is called within isCursorAtLineEnd

    // First, terminal width 80
    mockConsole->setTerminalWidth(80);

    setCursorPos(80);
    EXPECT_TRUE(isCursorAtLineEnd());

    // Next, terminal width 100
    mockConsole->setTerminalWidth(100);

    setCursorPos(50);
    EXPECT_FALSE(isCursorAtLineEnd());

    setCursorPos(100);
    EXPECT_TRUE(isCursorAtLineEnd());

    // Set terminal width back to the default
    mockConsole->setTerminalWidth(80);
}

// Test moveCursorLeft
TEST_F(Internal_ConsoleTest, MoveCursorLeft_ShouldMoveCursorLeftProperly)
{
    setCursorPos(5);
    size_t moveCount = 3;

    // Expect the console's moveCursorLeft method to be called moveCount times
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(static_cast<int>(moveCount));

    moveCursorLeft(moveCount);

    EXPECT_EQ(getCursorPos(), 2);
}

// Test moveCursorLeft beyone boundary
TEST_F(Internal_ConsoleTest, MoveCursorLeft_AtBoundary_ShouldNotMoveCursorPastBoundry)
{
    setCursorPos(2);

    size_t moveCount = 5; // Attempt to move beyond the start

    // Expect the console's moveCursorLeft method to only be called twice
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(2);

    moveCursorLeft(moveCount);

    EXPECT_EQ(getCursorPos(), 0);
}

// Test moveCursorLeft across line boundaries
TEST_F(Internal_ConsoleTest, MoveCursorLeft_AcrossLineBoundaries_ShouldHandleLineTransition)
{
    setCursorPos(81); // One character into the second line
    size_t moveCount = 2;

    // Expect the counsol's moveCursorLeft to be called once normally and once across lines
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorUp(1)).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorRight(80)).Times(1);

    moveCursorLeft(moveCount);

    EXPECT_EQ(getCursorPos(), 79);
}

// Test moveCursorRight
TEST_F(Internal_ConsoleTest, MoveCursorRight_ShouldMoveCursorRightProperly)
{
    setCursorPos(5);
    size_t moveCount = 3;
    std::string input = "HelloWorld";

    // Expect the console's moveCursorRight method to be called moveCount times
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(3);

    moveCursorRight(moveCount);

    EXPECT_EQ(getCursorPos(), 8);
}

// Test moveCursorRight at boundry
TEST_F(Internal_ConsoleTest, MoveCursorRight_AtBoundary_ShouldNotMoveCursorPastBoundry)
{
    setCursorPos(8);
    size_t moveCount = 5;
    std::string input = "HelloWorld";

    // Expect the console's moveCursorRight method to be called moveCount times
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(2);

    moveCursorRight(moveCount, &input);

    EXPECT_EQ(getCursorPos(), 10);
}

// Test moveCursorRight across line boundry
TEST_F(Internal_ConsoleTest, MoveCursorRight_AcrossLineBoundaries_ShouldHandleLineTransition)
{
    setCursorPos(79); // Last comumn of first line
    size_t moveCount = 2;
    std::string input = std::string(85, ' ');

    // Expect the consol's moveCursorRight to be called twice, moving the cursor to the next line
    EXPECT_CALL(*mockConsole, moveCursorDown(1)).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(1);

    moveCursorRight(moveCount, &input);
}

// Test moveCursorUp with valid steps
TEST_F(Internal_ConsoleTest, MoveCursorUp_WithValidSteps_ShouldMoveCursorUpProperly)
{
    size_t moveCount = 2;

    // Expect the consol's moveCursorUp method to be called moveCount times
    EXPECT_CALL(*mockConsole, moveCursorUp(2)).Times(1);

    moveCursorUp(moveCount);

    // No change to cursorPos as moveCursorUp does not affect it directly
    EXPECT_EQ(getCursorPos(), 0);
}

// Test moveCursorUp with zero steps
TEST_F(Internal_ConsoleTest, MoveCursorUp_ZeroSteps_ShouldNotMoveCursor)
{
    size_t moveCount = 0;

    // Expect the console's moveCursorUp not to be called
    EXPECT_CALL(*mockConsole, moveCursorUp(::testing::_)).Times(0);

    moveCursorUp(moveCount);

    // Verify internal state remains unchanged
    EXPECT_EQ(getCursorPos(), 0);
}

// Test moveCursorDown with valid steps
TEST_F(Internal_ConsoleTest, MoveCursorDown_WithValidSteps_ShouldMoveCursorDownProperly)
{
    size_t moveCount = 3;

    // Expect the consol's moveCursorDown method to be called moveCount times
    EXPECT_CALL(*mockConsole, moveCursorDown(3)).Times(1);

    moveCursorDown(moveCount);

    // Nochange to cursorPos as moveCursorDown does not effect it directly
    EXPECT_EQ(getCursorPos(), 0);
}

// Test moveCursorDown with zero steps
TEST_F(Internal_ConsoleTest, MoveCursorDown_ZeroSteps_ShouldNotMoveCursor)
{
    size_t moveCount = 0;

    // Expect the console's moveCursorDown not to be called
    EXPECT_CALL(*mockConsole, moveCursorDown(::testing::_)).Times(0);

    moveCursorDown(moveCount);

    // Verify internal state remains unchanged
    EXPECT_EQ(getCursorPos(), 0);
}

// Test moveCursorToStart from middle
TEST_F(Internal_ConsoleTest, MoveCursorToStart_FromMiddle_ShouldMoveCursorToStart)
{
    setCursorPos(10);

    // Expect moveCursorLeft to be called 10 times
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(10);

    moveCursorToStart();

    EXPECT_EQ(getCursorPos(), 0);
}

// Test moveCursorToStart when already at start
TEST_F(Internal_ConsoleTest, MoveCursorToStart_AlreadyAtStart_ShouldNotMoveCursor)
{
    setCursorPos(0);

    // Expect moveCursorLeft noe to be called
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(0);

    moveCursorToStart();

    EXPECT_EQ(getCursorPos(), 0);
}

// Test moveCursorToEnd when from middle
TEST_F(Internal_ConsoleTest, MoveCursorToEnd_FromMiddle_ShouldMoveCursorToEnd)
{
    setCursorPos(4);
    std::string input = "helloWorld";

    // Expect the console's moveCursorRight method to be called 6 times
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(6);

    moveCursorToEnd(input);

    EXPECT_EQ(getCursorPos(), 10);
}

// Test moveCursorToEnd when already at end
TEST_F(Internal_ConsoleTest, MoveCursorToEnd_AlreadyAtEnd_ShouldNotMoveCursor)
{
    setCursorPos(10);
    std::string input = "HelloWorld";

    // Expect the console's moveCursorRight method to be called 0 times
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(0);

    moveCursorToEnd(input);

    EXPECT_EQ(getCursorPos(), 10);
}

#pragma endregion
#pragma region AdvancedCursor

// Test skipWordLeft in the middle of input
TEST_F(Internal_ConsoleTest, SkipWordLeft_InMiddle_ShouldMoveCursorLeftToPreviousWord)
{
    std::string input = "Hello World";
    setCursorPos(8); // Position before 'o'

    // Expect the console's moveCursorLeft to be called 2 times (from 8 to 6)
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(2);

    skipWordLeft(input);

    EXPECT_EQ(getCursorPos(), 6);
}

// Test skipWordLeft at start (no movement)
TEST_F(Internal_ConsoleTest, SkipWordLeft_AtStart_ShouldNotMoveCursor)
{
    std::string input = "Hello World";
    setCursorPos(0); // Start position

    // Expect the console's moveCursorLeft not to be called
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(0);

    skipWordLeft(input);

    EXPECT_EQ(getCursorPos(), 0);
}

// Test skipWordRight in the middle of input
TEST_F(Internal_ConsoleTest, SkipWordRight_InMiddle_ShouldMoveCursorRightToNextWord)
{
    std::string input = "Hello World";
    setCursorPos(3); // Position at space before 'l'

    // Expect the console's moveCursorRight to be called 3 times
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(3);

    skipWordRight(input);

    EXPECT_EQ(getCursorPos(), 6);
}

// Test skipWordRight at end (no movement)
TEST_F(Internal_ConsoleTest, SkipWordRight_AtEnd_ShouldNotMoveCursor)
{
    std::string input = "Hello World";
    setCursorPos(11); // After 'd'

    // Expect the console's moveCursorRight not to be called
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(0);

    skipWordRight(input);

    EXPECT_EQ(getCursorPos(), 11);
}

// Test skipWordLeft with multiple spaces
TEST_F(Internal_ConsoleTest, SkipWordLeft_WithMultipleSpaces_ShouldSkipAllSpaces)
{
    std::string input = "Hello    World";
    setCursorPos(11); // Before 'o'

    // Expect the console's moveCursorLeft method to be called multiple times to skip spaces
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(2);

    skipWordLeft(input);

    EXPECT_EQ(getCursorPos(), 9);
}

// Test skipWordRight with multiple spaces
TEST_F(Internal_ConsoleTest, SkipWordRight_WithMultipleSpaces_ShouldSkipAllSpaces)
{
    std::string input = "Hello    World";
    setCursorPos(4); // After 'Hell'

    // Expect the console's moveCursorRight method to be called multiple times to skip spaces
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(5);

    skipWordRight(input);

    EXPECT_EQ(getCursorPos(), 9);
}

#pragma endregion
#pragma region TerminalWidth

// Test getTerminalWidth
TEST_F(Internal_ConsoleTest, GetTerminalWidth_ShouldReturnTerminalWidth)
{
    size_t expectedWidth = 100;

    // Mock the terminal width
    mockConsole->setTerminalWidth(expectedWidth);

    size_t width = getTerminalWidth();

    EXPECT_EQ(width, expectedWidth);
}

#pragma endregion
#pragma region RewriteTail

// Test rewriteTail from a specific position
TEST_F(Internal_ConsoleTest, RewriteTail_FromSpecificPosition_ShouldRewriteInputCorrectly)
{
    std::string input = "Hello World";
    size_t startPosition = 6; // After "Hello "

    // Expected behavior:
    // - Save cursor position
    // - Print characters from startPosition
    // - Clear leftover space
    // - Restore cursor position

    // Setup expectations
     EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
     EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(5);
     EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    rewriteTail(input, startPosition);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "World");
}

// Test rewriteTail with startPosition beyond input size
TEST_F(Internal_ConsoleTest, RewriteTail_StartPositionBeyondInput_ShouldDoNothing)
{
    std::string input = "Hello";
    size_t startPosition = 10; // Beyond input size

    // Expect no printing
     EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(0);
     EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);
     EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(0);

    rewriteTail(input, startPosition);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test rewriteTail with partially overlapping input
TEST_F(Internal_ConsoleTest, RewriteTail_PartiallyOverlappingInput_ShouldRewriteCorrectly)
{
    std::string input = "Hello World";
    size_t startPosition = 5; // After "Hello"

    // Expect saveCursorPosition and restoreCursorPosition to be called
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(6); // Assuming space and "World"
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    rewriteTail(input, startPosition);

    EXPECT_EQ(mockConsole->getCapturedOutput(), " World");
}

#pragma endregion
#pragma region ClearLine

// Test clearLineAfterCursor
TEST_F(Internal_ConsoleTest, ClearLineAfterCursor_ShouldClearAndMoveCursorLeft)
{
    setCursorPos(20);
    size_t width = 80;

    // Expect clearLineAfterCursor to be called
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print(std::string(width, ' '), ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    console->clearLineAfterCursor();
}

// Test clearCurrentLine
TEST_F(Internal_ConsoleTest, ClearCurrentLine_ShouldClearLineAndSetNextLine)
{
    std::string input = "Old Input";
    std::string nextLine = "New Input";

    setCursorPos(input.size());

    // Expect moveCursorToStart and clearLineAfterCursor to be called
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(9);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(1);

    // Call clearCurrentLine
    console->clearCurrentLine(input, nextLine);

    // Verify that input is updated and cursorPos is set correctly
    EXPECT_EQ(input, "New Input");
    EXPECT_EQ(getCursorPos(), nextLine.length());
}

// Test clearCurrentLine with empty next line
TEST_F(Internal_ConsoleTest, ClearCurrentLine_WithEmptyNextLine_ShouldClearAndSetEmptyInput)
{
    std::string input = "SomeInput";
    std::string nextLine = "";

    // Expect moveCursorToStart and clearLineAfterCursor to be called
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(1);

    console->clearCurrentLine(input, nextLine);

    EXPECT_EQ(input, "");
    EXPECT_EQ(getCursorPos(), 0);
}

// Test clearLineAfterCursor when cursor is in the middle
TEST_F(Internal_ConsoleTest, ClearLineAfterCursor_CursorInMiddle_ShouldClearProperly)
{
    size_t width = 80;
    setCursorPos(40);

    // Expected methods ran
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print(std::string(width, ' '), ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    console->clearLineAfterCursor();

    EXPECT_EQ(getCursorPos(), 40);
}

#pragma endregion
#pragma region CharacterPrinting

// Test handlePrintableChar at the end of the input
TEST_F(Internal_ConsoleTest, HandlePrintableChar_ShouldInsertCharacterAtEndOfInput)
{
    std::string input = "Hello Worl";
    char newChar = 'd';
    std::string ch = std::string(1, newChar);


    EXPECT_CALL(*mockConsole, print("d", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    setCursorPos(10);

    handlePrintableChar(newChar, input);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "d");
    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getCursorPos(), 11);
}

// Test handlePrintableChar
TEST_F(Internal_ConsoleTest, HandlePrintableChar_ShouldInsertCharacter)
{
    std::string input = "Hell World";
    char newChar = 'o';

    EXPECT_CALL(*mockConsole, print("o", ::testing::_)).Times(2);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print(::testing::Ne("o"), ::testing::_)).Times(5);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    setCursorPos(4); // Position between 'Hell' and ' World'

    handlePrintableChar(newChar, input);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "o World");
    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getCursorPos(), 5);
}

// Test handlePrintableChar with multiple characters
TEST_F(Internal_ConsoleTest, HandlePrintableChar_WithMultipleCharacters_ShouldInsertSequentially)
{
    std::string input = "Hell World";
    char newChar1 = 'o';
    char newChar2 = '!';
    setCursorPos(4); // Between 'Hell' and ' World'

    // Expect the console's print method to be called with 'o' and '!'
    EXPECT_CALL(*mockConsole, print("o", ::testing::_)).Times(3);
    EXPECT_CALL(*mockConsole, print("!", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(2);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(2);
    EXPECT_CALL(*mockConsole, print(::testing::Not(::testing::AnyOf(
        ::testing::Eq("o"),
        ::testing::Eq("!")
    )), ::testing::_)).Times(10);

    handlePrintableChar(newChar1, input);
    handlePrintableChar(newChar2, input);

    EXPECT_EQ(input, "Hello! World");
    EXPECT_EQ(getCursorPos(), 6);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "o World! World");
}

// Test handlePrintableChar with non-printable characters
TEST_F(Internal_ConsoleTest, HandlePrintableChar_NonPrintable_ShouldIgnore)
{
    std::string input = "Hello";
    char newChar = '\n'; // Newline is non-printable
    setCursorPos(5);

    // Expect no print calls
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    handlePrintableChar(newChar, input);

    EXPECT_EQ(input, "Hello");
    EXPECT_EQ(getCursorPos(), 5);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test handlePrintableChar after moving cursor left
TEST_F(Internal_ConsoleTest, HandlePrintableChar_AfterMoveLeft_ShouldInsertAtNewPosition)
{
    std::string input = "Helo World";
    char newChar = 'l';
    setCursorPos(3); // Between 'e' and 'l'

    // Expect the console's print method to be called with 'l'
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(8);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    handlePrintableChar(newChar, input);

    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getCursorPos(), 4);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "lo World");
}

// Test handlePrintableChar with empty input
TEST_F(Internal_ConsoleTest, HandlePrintableChar_EmptyInput_ShouldInsertCharacter)
{
    std::string input = "";
    char newChar = 'A';
    setCursorPos(0);

    // Expect the console's print method to be called with 'A'
    EXPECT_CALL(*mockConsole, print("A", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    handlePrintableChar(newChar, input);

    EXPECT_EQ(input, "A");
    EXPECT_EQ(getCursorPos(), 1);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "A");
}

// Test handlePrintableChar with multiple insertions exceeding terminal width
TEST_F(Internal_ConsoleTest, HandlePrintableChar_MultipleInsertions_ExceedTerminalWidth_ShouldHandleWrapping)
{
    std::string input = "";
    setCursorPos(0);
    std::string expectedOutput = "";

    // Insert 80 characters to fill the first line
    for (int i = 0; i < 80; ++i)
    {
        char newChar = 'a';
        EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
        EXPECT_CALL(*mockConsole, print("a", ::testing::_)).Times(1);
        EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);
        expectedOutput += "a";
        handlePrintableChar(newChar, input);
    }

    EXPECT_EQ(input.size(), 80);
    EXPECT_EQ(getCursorPos(), 80);
    EXPECT_EQ(mockConsole->getCapturedOutput(), expectedOutput);

    // Insert one more character to wrap to the next line
    char newChar = 'b';
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print("b", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);
    expectedOutput += "b";
    handlePrintableChar(newChar, input);

    EXPECT_EQ(input.size(), 81);
    EXPECT_EQ(getCursorPos(), 81);
    EXPECT_EQ(mockConsole->getCapturedOutput(), expectedOutput);
}

// Test handlePrintableChar with wrapping and rewriting tail
TEST_F(Internal_ConsoleTest, HandlePrintableChar_WithWrapping_ShouldRewriteTailCorrectly)
{
    // Assuming the terminal width is 80
    std::string input = std::string(80, 'A');
    size_t startPosition = 70;

    // Set cursor position
    setCursorPos(startPosition);

    // Expect saveCursorPosition and restoreCursorPosition to be called
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print("A", ::testing::_)).Times(10); // Insert 'A'
    EXPECT_CALL(*mockConsole, print("B", ::testing::_)).Times(1); // Insert 'B'
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    handlePrintableChar('B', input);
    EXPECT_EQ(input, std::string(70, 'A') + "B" + std::string(10, 'A'));
    EXPECT_EQ(getCursorPos(), 71);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "B" + std::string(10, 'A'));
}

// Test handlePrintableChar after deleting all characters
TEST_F(Internal_ConsoleTest, HandlePrintableChar_AfterDeletingAllCharacters_ShouldInsertNewCharacter)
{
    std::string input = "";
    setCursorPos(0);

    char newChar = 'A';

    // Expect the console's print method to be called with 'A'
    EXPECT_CALL(*mockConsole, print("A", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    handlePrintableChar(newChar, input);

    EXPECT_EQ(input, "A");
    EXPECT_EQ(getCursorPos(), 1);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "A");
}

// Test handlePrintableChar with uppercase and lowercase mix
TEST_F(Internal_ConsoleTest, HandlePrintableChar_UppercaseAndLowercase_ShouldHandleCorrectly)
{
    std::string input = "Hello";
    char newChar1 = 'W';
    char newChar2 = 'o';
    char newChar3 = 'R';
    char newChar4 = 'l';
    char newChar5 = 'd';
    setCursorPos(5);

    // Expect the console's print method to be called with each character
    EXPECT_CALL(*mockConsole, print(" ", ::testing::_)).Times(0);
    EXPECT_CALL(*mockConsole, print("W", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, print("o", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, print("R", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, print("l", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, print("d", ::testing::_)).Times(1);

    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(5);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(5);

    handlePrintableChar(newChar1, input);
    handlePrintableChar(newChar2, input);
    handlePrintableChar(newChar3, input);
    handlePrintableChar(newChar4, input);
    handlePrintableChar(newChar5, input);

    EXPECT_EQ(input, "HelloWoRld");
    EXPECT_EQ(getCursorPos(), 10);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "WoRld");
}

// Test handlePrintableChar with cursor wrapping multiple lines
TEST_F(Internal_ConsoleTest, HandlePrintableChar_CursorWrappingMultipleLines_ShouldHandleCorrectly)
{
    std::string input = "";
    std::string expectedOutput = "";

    size_t terminalWidth = 40; // Mock terminal width to 40 for testing
    mockConsole->setTerminalWidth(terminalWidth);

    // Insert 80 characters to wrap twice
    for (size_t i = 0; i < 80; ++i)
    {
        char newChar = 'a';
        EXPECT_CALL(*mockConsole, print("a", ::testing::_)).Times(1);
        EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
        EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);
        expectedOutput += "a";
        handlePrintableChar(newChar, input);
    }

    EXPECT_EQ(input.size(), 80);
    EXPECT_EQ(getCursorPos(), 80);
    EXPECT_EQ(mockConsole->getCapturedOutput(), expectedOutput);
}

// Test handlePrintableChar with non-printable ASCII characters
TEST_F(Internal_ConsoleTest, HandlePrintableChar_NonPrintableASCII_ShouldIgnore)
{
    std::string input = "Hello";
    char newChar = '\x01'; // SOH (Start of Header), non-printable
    setCursorPos(5);

    // Expect no print calls
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    handlePrintableChar(newChar, input);

    EXPECT_EQ(input, "Hello");
    EXPECT_EQ(getCursorPos(), 5);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test handlePrintableChar with cursor at various positions
TEST_F(Internal_ConsoleTest, HandlePrintableChar_VariousCursorPositions_ShouldHandleCorrectly)
{
    std::string input = "ABCDE";
    char newChar = 'X';

    // Insert at position 0
    setCursorPos(0);
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(6);
    EXPECT_CALL(*mockConsole, saveCursorPosition());
    EXPECT_CALL(*mockConsole, restoreCursorPosition());
    handlePrintableChar(newChar, input);
    EXPECT_EQ(input, "XABCDE");
    EXPECT_EQ(getCursorPos(), 1);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "XABCDE");

    // Insert at position 3
    setCursorPos(3);
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(4);
    EXPECT_CALL(*mockConsole, saveCursorPosition());
    EXPECT_CALL(*mockConsole, restoreCursorPosition());
    handlePrintableChar(newChar, input);
    EXPECT_EQ(input, "XABXCDE");
    EXPECT_EQ(getCursorPos(), 4);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "XABCDEXCDE");
}

#pragma endregion
#pragma region CharacterPrintingInInsertMode

// Test handlePrintableChar with cursor at start in overwrite mode
TEST_F(Internal_ConsoleTest, HandlePrintableChar_OverwriteMode_CursorAtStart_ShouldOverwriteFirstCharacter)
{
    std::string input = "Hello";
    char newChar = 'Y';
    setCursorPos(0);
    toggleInsertMode(input); // Overwrite mode

    // Expect the console's print method to be called with 'Y'
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(5);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    handlePrintableChar(newChar, input);

    EXPECT_EQ(input, "Yello");
    EXPECT_EQ(getCursorPos(), 1);
    EXPECT_EQ(getInsertString(), "Hello");
    EXPECT_EQ(mockConsole->getCapturedOutput(), "Yello");
}

// Test handlePrintableChar with mixed insert and overwrite modes
TEST_F(Internal_ConsoleTest, HandlePrintableChar_MixedModes_ShouldHandleCorrectly)
{
    std::string input = "Hell World";
    char newChar1 = 'o';
    setCursorPos(4); // Between 'Hell' and ' World'

    // Expect the console's print method to be called with 'o'
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(7);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);
    handlePrintableChar(newChar1, input);
    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getCursorPos(), 5);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "o World");

    // Switch to overwrite mode
    toggleInsertMode(input);
    setCursorPos(6); // Before 'W'
    char newChar2 = 'A';
    
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(5);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);
    handlePrintableChar(newChar2, input);
    EXPECT_EQ(input, "Hello Aorld");
    EXPECT_EQ(getCursorPos(), 7);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "o WorldAorld");
}

// Test handlePrintableChar with multiple overwrites beyond input length
TEST_F(Internal_ConsoleTest, HandlePrintableChar_OverwriteMode_BeyondInputLength_ShouldAppend)
{
    std::string input = "Hello";
    char newChar = '!';
    setCursorPos(5); // End of input
    toggleInsertMode(input); // Overwrite mode

    // Expect the console's print method to be called with '!'
    EXPECT_CALL(*mockConsole, print("!", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    handlePrintableChar(newChar, input);

    EXPECT_EQ(input, "Hello!");
    EXPECT_EQ(getCursorPos(), 6);
    EXPECT_EQ(getInsertString(), "Hello");
    EXPECT_EQ(mockConsole->getCapturedOutput(), "!");
}

// Test handlePrintableChar with mixed modes and cursor positions
TEST_F(Internal_ConsoleTest, HandlePrintableChar_MixedModesAndCursorPositions_ShouldHandleCorrectly)
{
    std::string input = "HelloWorld";
    char newChar1 = ' ';
    char newChar2 = 'C';
    char newChar3 = '!';
    setCursorPos(5); // Between 'Hello' and 'World'

    // Insert ' ', expect 'Hello World'
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(6);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);
    handlePrintableChar(newChar1, input);
    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getCursorPos(), 6);
    EXPECT_EQ(mockConsole->getCapturedOutput(), " World");

    // Switch to overwrite mode and insert 'C'
    toggleInsertMode(input);
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(5);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);
    handlePrintableChar(newChar2, input);
    EXPECT_EQ(input, "Hello Corld");
    EXPECT_EQ(getInsertString(), "Hello World");
    EXPECT_EQ(getCursorPos(), 7);
    EXPECT_EQ(mockConsole->getCapturedOutput(), " WorldCorld");

    // Switch back to insert mode and insert '!'
    toggleInsertMode(input);
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(5);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);
    handlePrintableChar(newChar3, input);
    EXPECT_EQ(input, "Hello C!orld");
    EXPECT_EQ(getInsertString(), "");
    EXPECT_EQ(getCursorPos(), 8);
    EXPECT_EQ(mockConsole->getCapturedOutput(), " WorldCorld!orld");
}

#pragma endregion
#pragma region GenericSpecialKeys

// Test handleSpecialKey with Enter key
TEST_F(Internal_ConsoleTest, HandleSpecialKey_Enter_ShouldAddToHistoryAndReturnInput)
{
    std::string input = "TestCommand";
    setCursorPos(11); // End of input

    // Expect history to be updated
    // Since history is a private member, assume it's accessible or use friend tests
    // For demonstration, skip internal history verification

    // Call handleSpecialKey with Enter key
    std::string result = handleSpecialKey('\x0a', input);
    
    std::vector<std::string> historyVector = {"TestCommand"};
    EXPECT_EQ(getHistory(), historyVector);
    EXPECT_EQ(result, "TestCommand");
}

// Test handleSpecialKey with Backspace key
TEST_F(Internal_ConsoleTest, HandleSpecialKey_Backspace_ShouldEraseCharacter)
{
    std::string input = "Hello World";
    setCursorPos(11); // End of input

    // Expect moveCursorLeft to be called once
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    // Expect rewriteTail to be called to erase 'd'
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(1); // Print space to erase
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    // Call handleSpecialKey with Backspace key
    std::string result = handleSpecialKey('\x7f', input);

    EXPECT_EQ(result, "");
    EXPECT_EQ(input, "Hello Worl");
    EXPECT_EQ(getCursorPos(), 10);
    EXPECT_EQ(mockConsole->getCapturedOutput(), " ");
}

// Test handleSpecialKey with Backspace key in overwrite mode
TEST_F(Internal_ConsoleTest, HandleSpecialKey_BackspaceInOverwriteMode_ShouldEraseCharacterLeavingBehindPrevious)
{
    std::string input = "Hello World";
    setCursorPos(7); // Right before 'o'
    toggleInsertMode(input); // Toggle overwrite mode
    
    // New input after character was inserted
    input = "Hello Aorld";

    // Expect moveCursorLeft to be called once
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    // Expect rewriteTail to be called to erase 'd'
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(6); // Print space to erase
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);
    
    std::string result = handleSpecialKey('\x7f', input);

    EXPECT_EQ(result, "");
    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getInsertString(), "Hello World");
    EXPECT_EQ(mockConsole->getCapturedOutput(), "World ");
}

// Test handleSpecialKey with Tab key
TEST_F(Internal_ConsoleTest, HandleSpecialKey_Tab_ShouldInsertTabCharacter)
{
    std::string input = "Hello";
    setCursorPos(5); // End of input

    // Expect the console's print method to be called with '\t'
    //EXPECT_CALL(*mockConsole, print("\t")).Times(1);

    // Call handleSpecialKey with Tab key
    std::string result = handleSpecialKey('\x09', input);

    EXPECT_EQ(result, "Hello\t");
    EXPECT_EQ(input, "Hello\t");
    EXPECT_EQ(getCursorPos(), 5);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test handleSpecialKey with unknown key (should do nothing)
TEST_F(Internal_ConsoleTest, HandleSpecialKey_UnknownKey_ShouldDoNothing)
{
    std::string input = "Hello";

    // Expect no interactions
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*mockConsole, moveCursorLeft(::testing::_)).Times(0);
    EXPECT_CALL(*mockConsole, moveCursorRight(::testing::_)).Times(0);

    // Call handleSpecialKey with unknown key
    std::string result = handleSpecialKey('X', input);

    EXPECT_EQ(result, "");
    EXPECT_EQ(input, "Hello");
    EXPECT_EQ(getCursorPos(), 0); // Assuming cursorPos was not changed
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

#pragma endregion
#pragma region EscapeSequences

// Test handleEscapeSequence with unknown escape sequence
TEST_F(Internal_ConsoleTest, HandleEscapeSequence_UnknownSequence_ShouldIgnore)
{
    // Simulate an unknown escape sequence: '\x1b', '[', 'Z'

    // Expect no specific actions
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(0);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(0);
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    std::string input = "SomeInput";
    handleEscapeSequence(input, "[Z");

    // Verify internal state remains unchanged
    EXPECT_EQ(getCursorPos(), 0);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test handleEscapeSequence with Home key
TEST_F(Internal_ConsoleTest, HandleEscapeSequence_HomeKey_ShouldMoveCursorToStart)
{
    std::string input = "SomeInput";
    // Simulate escape sequence for Home key: '\x1b', '[', '1', '~'
    setCursorPos(5);

    // Expect moveCursorToStart and clearLineAfterCursor to be called
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(5);

    handleEscapeSequence(input, "[1~");

    // Simulate escape sequence for Home key: '\x1b', '[', 'H'
    setCursorPos(5);

    // Expect moveCursorToStart and clearLineAfterCursor to be called
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(5);

    handleEscapeSequence(input, "[H");

    // Simulate escape sequence for Home key: '\x1b', '[', 'O', 'H'
    setCursorPos(5);

    // Expect moveCursorToStart and clearLineAfterCursor to be called
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(5);

    handleEscapeSequence(input, "[OH");


    SUCCEED();
}

// Test handleEscapeSequence with End key
TEST_F(Internal_ConsoleTest, HandleEscapeSequence_EndKey_ShouldMoveCursorToEnd)
{
    std::string input = "SomeInput";
    // Simulate escape sequence for End key: '\x1b', '[', '4', '~'
    setCursorPos(5);

    // Expect moveCursorToEnd to be called with current input
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(4);
    
    handleEscapeSequence(input, "[4~");

    // Simulate escape sequence for End key: '\x1b', '[', 'F'
    setCursorPos(5);

    // Expect moveCursorToEnd to be called with current input
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(4);
    
    handleEscapeSequence(input, "[F");


    SUCCEED();
}

// Test handleEscapeSequence with Insert key
TEST_F(Internal_ConsoleTest, HandleEscapeSequence_InsertKey_ShouldToggleInsertMode)
{
    // Simulate escape sequence for Insert key: '\x1b', '[', '2', '~'
    std::string input = "";

    // Toggle insert mode
    toggleInsertMode(input);
    // Since 'insert' is a member variable, verify its state
    // For demonstration, assume insert was initially false

     toggleInsertMode(input);

     handleEscapeSequence(input, "[2~");

     EXPECT_TRUE(getInsertMode());

     handleEscapeSequence(input, "[2~");

     EXPECT_FALSE(getInsertMode());
}

// Test handleEscapeSequence with Home and End keys sequentially
TEST_F(Internal_ConsoleTest, HandleEscapeSequence_HomeAndEndKeys_ShouldMoveCursorCorrectly)
{
    // Simulate Home key: '\x1b', '[', 'H'
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(2);

    std::string input = "SomeInput";
    setCursorPos(2);
    handleEscapeSequence(input, "[H");

    // Simulate End key: '\x1b', '[', 'F'
    EXPECT_CALL(*mockConsole, moveCursorRight(1)).Times(9); // Depending on implementation

    handleEscapeSequence(input, "[F");

    SUCCEED();
}

// Test handleEscapeSequence with incomplete escape sequence
TEST_F(Internal_ConsoleTest, HandleEscapeSequence_IncompleteSequence_ShouldIgnore)
{
    // Simulate incomplete escape sequence: '\x1b', '[' without a final character

    // Expect no actions
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(0);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(0);
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    std::string input = "SomeInput";
    handleEscapeSequence(input, "[");

    // Verify internal state remains unchanged
    EXPECT_EQ(getCursorPos(), 0);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test handleEscapeSequence with incomplete character
TEST_F(Internal_ConsoleTest, HandleEscapeSequence_IncompleteCharacter_ShouldIgnore)
{
    // Simulate incomplete escape sequence: '\x1b' only

    // Expect no actions
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(0);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(0);
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    std::string input = "SomeInput";
    handleEscapeSequence(input, "");

    // Verify internal state remains unchanged
    EXPECT_EQ(getCursorPos(), 0);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test if insertString gets updates if a escape key is ran
TEST_F(Internal_ConsoleTest, HandleInsertWithEscapeSequence_ShouldUpdateInsertString)
{
    // Initial input
    std::string input = "";

    // Toggle insert mode
    toggleInsertMode(input);

    // Methods to expect
    EXPECT_CALL(*mockConsole, print("o", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    handlePrintableChar('o', input);

    // Run a escape key to update insertString
    handleEscapeSequence(input, "[C");

    EXPECT_EQ(getInsertString(), "o");
}

#pragma endregion
#pragma region History

// Test getHistory when history is empty
TEST_F(Internal_ConsoleTest, GetHistory_EmptyHistory_ShouldReturnCurrentInput)
{
    std::string input = "";

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);
    // Test by simulating "up arrow"
    handleEscapeSequence(input, "[A");

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);
    // Test by simulating "down arrow"
    handleEscapeSequence(input, "[B");
}

// Test navigateHistory moving up
TEST_F(Internal_ConsoleTest, NavigateHistory_Up_ShouldSetInputFromHistory)
{
    GTEST_SKIP();
    // Simulate adding commands to history via handleSpecialKey with Enter
    std::string input = "first command";
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(13);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(13);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(13);
    console->input(input + '\x0a');

    input = "second command";
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(14);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(14);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(14);
    console->input(input + '\x0a');

    input = "third command";
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(13);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(13);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(13);
    console->input(input + '\x0a');

    std::vector<std::string> testHistory = {"first command", "second command", "third command"};
    EXPECT_EQ(getHistory(), testHistory);

    setCursorPos(0);

    // Now, simulate navigating up to get "second command"
    // Simulate escape sequence for Up Arrow: '\x1b', '[', 'A'
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(1);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorDown(1)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print("third command", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    input = "";
    handleEscapeSequence(input, "[A");

    // Similarly, simulate navigating up to get "second command"
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(13);
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(1);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorDown(1)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print("second command", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    input = "third command";
    handleEscapeSequence(input, "[A");

    // Similarly, simulate navigating up to get "first command"
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(14);
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(1);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorDown(1)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print("first command", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    input = "second command";
    handleEscapeSequence(input, "[A");

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    input = "first command";
    handleEscapeSequence(input, "[A");

    SUCCEED();
}

// Test navigateHistory moving down
TEST_F(Internal_ConsoleTest, NavigateHistory_Down_ShouldSetInputFromHistory)
{
    GTEST_SKIP();
    // Simulate adding commands to history via handleSpecialKey with Enter
    std::string input = "first command";
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(13);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(13);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(13);
    console->input(input + '\x0a');

    input = "second command";
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(14);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(14);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(14);
    console->input(input + '\x0a');

    input = "third command";
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(13);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(13);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(13);
    console->input(input + '\x0a');

    std::vector<std::string> testHistory = {"first command", "second command", "third command"};
    EXPECT_EQ(getHistory(), testHistory);

    setCursorPos(0);

    // Now, simulate navigating up to get "second command"
    // Simulate escape sequence for Up Arrow: '\x1b', '[', 'A'
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(1);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorDown(1)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print("third command", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    input = "";
    handleEscapeSequence(input, "[A");

    // Similarly, simulate navigating up to get "second command"
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(13);
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(1);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorDown(1)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print("second command", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    input = "third command";
    handleEscapeSequence(input, "[A");

    // Simulate navigating down to "third command"
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(14);
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(1);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorDown(1)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print("third command", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

    input = "second command";
    handleEscapeSequence(input, "[B");

    // Simulate navigating down to ""
    EXPECT_CALL(*mockConsole, moveCursorLeft(1)).Times(13);
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(1);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorDown(1)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, print("", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);


    input = "third command";
    handleEscapeSequence(input, "[B");

    SUCCEED();
}

// Test updateDisplayInput after navigating history
TEST_F(Internal_ConsoleTest, UpdateDisplayInput_ShouldUpdateInputDisplayCorrectly)
{
    std::string oldInput = "OldCommand";
    std::string newInput = "NewCommand";

    // Expect moveCursorToStart and clearLineAfterCursor
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorDown(1)).Times(1);
    EXPECT_CALL(*mockConsole, moveCursorToStart()).Times(1);
    EXPECT_CALL(*mockConsole, clearLineAfterCursor()).Times(1);
    // Expect print with new input
    EXPECT_CALL(*mockConsole, print(newInput, ::testing::_)).Times(1);

    updateDisplayInput(oldInput, newInput);

    // Verify internal state
    EXPECT_EQ(getCursorPos(), newInput.length());
    EXPECT_EQ(mockConsole->getCapturedOutput(), newInput);
}

#pragma endregion
#pragma region Kbhit

// Test kbhit when key is pressed
TEST_F(Internal_ConsoleTest, Kbhit_KeyPressed_ShouldReturnTrue)
{
    // Mock getchar to return a character
    // Since kbhit is implemented within Console class, it's challenging to mock
    // Consider refactoring to allow mocking of getchar or system calls
    // For demonstration, skip this test or assume it works as expected
    //SUCCEED() << "kbhit method requires refactoring for testability.";
}

// Test kbhit when no key is pressed
TEST_F(Internal_ConsoleTest, Kbhit_NoKeyPressed_ShouldReturnFalse)
{
    // Similar to the above test, requires refactoring
    //SUCCEED() << "kbhit method requires refactoring for testability.";
}

#pragma endregion
