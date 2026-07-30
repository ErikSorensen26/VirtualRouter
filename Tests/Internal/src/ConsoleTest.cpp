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
    void moveCursorToStart() {console->moveCursorToStart();}
    void moveCursorToEnd(std::string& input) {console->moveCursorToEnd(input);}
    void skipWordLeft(std::string& input) {console->skipWordLeft(input);}
    void skipWordRight(std::string& input) {console->skipWordRight(input);}
    void toggleInsertMode(std::string& input) {handleEscapeSequence(input, "[2~");}
    bool getInsertMode() {return console->insert;}
    std::string getInsertString() {return console->insertString;}
    void renderInput(const std::string& input) {console->renderInput(input);}
    void setInitialLineLength(size_t len) {console->initialLineLength = len;}

    void seedDisplay(const std::string& text, size_t cursor)
    {
        setCursorPos(text.size());
        console->renderInput(text);
        setCursorPos(cursor);
        mockConsole->resetCapturedOutput();
    }
    void handlePrintableChar(char hInput, std::string& input) {console->handlePrintableChar(hInput, input);}
    std::string handleSpecialKey(char hInput, std::string& input)
    {
        return console->handleSpecialKey(hInput, input) ? input : std::string();
    }
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

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    moveCursorRight(40, nullptr);
    EXPECT_EQ(getCursorPos(), 80);
    EXPECT_TRUE(isCursorAtLineEnd());

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

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

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

    // Cursor movement is now purely logical: the terminal is not touched here,
    // FrameBuffer repositions on the next render.
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    moveCursorRight(moveCount, &input);

    EXPECT_EQ(getCursorPos(), 81);
}

// Test that a render spanning multiple rows moves the cursor down
TEST_F(Internal_ConsoleTest, RenderInput_SpanningRows_ShouldEmitDownwardCursorMove)
{
    // Width 80 with an 8-column prompt: 75 chars of input reach the second row.
    setInitialLineLength(8);
    std::string input(75, 'x');
    setCursorPos(input.size());

    renderInput(input);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[9G" + input);
}

// Test that re-rendering identical content emits nothing
TEST_F(Internal_ConsoleTest, RenderInput_UnchangedContent_ShouldEmitNothing)
{
    setInitialLineLength(8);
    std::string input = "hello";
    setCursorPos(input.size());

    renderInput(input);
    mockConsole->resetCapturedOutput();

    // The frame diff suppresses output when nothing changed.
    renderInput(input);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test that shortening the input erases the characters left behind
TEST_F(Internal_ConsoleTest, RenderInput_ShorterContent_ShouldEraseTrailingCharacters)
{
    setInitialLineLength(8);
    std::string first = "abc";
    setCursorPos(first.size());
    renderInput(first);
    mockConsole->resetCapturedOutput();

    std::string second = "zz";
    setCursorPos(second.size());
    renderInput(second);

    // Column 9 (1-based) is the first input cell: rewrite "zz", blank the orphaned
    // 'c', then park the cursor back at column 11.
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[9Gzz \033[11G");
}

// Test that editing mid-string only rewrites from the edit point
TEST_F(Internal_ConsoleTest, RenderInput_MidStringEdit_ShouldRewriteOnlyTheChangedTail)
{
    setInitialLineLength(8);
    setCursorPos(3);
    renderInput("helo");
    mockConsole->resetCapturedOutput();

    setCursorPos(4);
    renderInput("hello");

    // "hel" is unchanged, so only "lo" is redrawn before the cursor is restored.
    EXPECT_EQ(mockConsole->getCapturedOutput(), "lo\033[13G");
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
#pragma region RenderInput

// Test that appending only emits the newly added characters
TEST_F(Internal_ConsoleTest, RenderInput_AppendedTail_ShouldEmitOnlyTheNewCharacters)
{
    setInitialLineLength(0);
    setCursorPos(6);
    renderInput("Hello ");
    mockConsole->resetCapturedOutput();

    setCursorPos(11);
    renderInput("Hello World");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "World");
}

// Test that rendering the same text twice is a no-op
TEST_F(Internal_ConsoleTest, RenderInput_RepeatedIdenticalRender_ShouldDoNothing)
{
    setInitialLineLength(0);
    setCursorPos(5);
    renderInput("Hello");
    mockConsole->resetCapturedOutput();

    renderInput("Hello");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test that a changed interior character redraws from the change onward
TEST_F(Internal_ConsoleTest, RenderInput_PartiallyOverlappingInput_ShouldRewriteFromFirstDifference)
{
    setInitialLineLength(0);
    setCursorPos(11);
    renderInput("Hello World");
    mockConsole->resetCapturedOutput();

    renderInput("Hellx World");

    // Only column 5 (1-based) differs; the cursor then returns to column 12.
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[5Gx\033[12G");
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

    seedDisplay(input, 10);

    EXPECT_CALL(*mockConsole, print("d", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, saveCursorPosition()).Times(1);
    EXPECT_CALL(*mockConsole, restoreCursorPosition()).Times(1);

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

    seedDisplay(input, 4); // Position between 'Hell' and ' World'

    handlePrintableChar(newChar, input);

    // One render seeks to the insertion point, rewrites the tail from there,
    // then puts the cursor back just after the inserted character.
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[5Go World\033[6G");
    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getCursorPos(), 5);
}

// Test handlePrintableChar with multiple characters
TEST_F(Internal_ConsoleTest, HandlePrintableChar_WithMultipleCharacters_ShouldInsertSequentially)
{
    std::string input = "Hell World";
    char newChar1 = 'o';
    char newChar2 = '!';
    seedDisplay(input, 4); // Between 'Hell' and ' World'

    // One render per inserted character.
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(2);

    handlePrintableChar(newChar1, input);
    handlePrintableChar(newChar2, input);

    EXPECT_EQ(input, "Hello! World");
    EXPECT_EQ(getCursorPos(), 6);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[5Go World\033[6G! World\033[7G");
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
    seedDisplay(input, 3); // Between 'e' and 'l'

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(1);

    handlePrintableChar(newChar, input);

    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getCursorPos(), 4);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[4Glo World\033[5G");
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

    // Appending at the end of the line, so each render emits exactly the one
    // new character and nothing else.
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(81);

    // Insert 80 characters to fill the first line
    for (int i = 0; i < 80; ++i)
    {
        char newChar = 'a';
        expectedOutput += "a";
        handlePrintableChar(newChar, input);
    }

    EXPECT_EQ(input.size(), 80);
    EXPECT_EQ(getCursorPos(), 80);
    EXPECT_EQ(mockConsole->getCapturedOutput(), expectedOutput);

    // Insert one more character to wrap to the next line
    char newChar = 'b';
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

    seedDisplay(input, startPosition);

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(1);

    handlePrintableChar('B', input);
    EXPECT_EQ(input, std::string(70, 'A') + "B" + std::string(10, 'A'));
    EXPECT_EQ(getCursorPos(), 71);
    EXPECT_EQ(mockConsole->getCapturedOutput(),
              "\033[1A\033[71GB\033[1B\033[1GA\033[1A\033[72G");
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
    seedDisplay(input, 5);

    // Appending at the end, so each render emits just its one new character.
    EXPECT_CALL(*mockConsole, print(" ", ::testing::_)).Times(0);
    EXPECT_CALL(*mockConsole, print("W", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, print("o", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, print("R", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, print("l", ::testing::_)).Times(1);
    EXPECT_CALL(*mockConsole, print("d", ::testing::_)).Times(1);

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

    // Appending at the end of the line, so each render emits exactly the one
    // new character and nothing else.
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(80);

    // Insert 80 characters to wrap twice
    for (size_t i = 0; i < 80; ++i)
    {
        char newChar = 'a';
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
    seedDisplay(input, 0);
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(2);
    handlePrintableChar(newChar, input);
    EXPECT_EQ(input, "XABCDE");
    EXPECT_EQ(getCursorPos(), 1);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[1GXABCDE\033[2G");

    // Insert at position 3
    setCursorPos(3);
    handlePrintableChar(newChar, input);
    EXPECT_EQ(input, "XABXCDE");
    EXPECT_EQ(getCursorPos(), 4);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[1GXABCDE\033[2GABXCDE\033[5G");
}

#pragma endregion
#pragma region CharacterPrintingInInsertMode

// Test handlePrintableChar with cursor at start in overwrite mode
TEST_F(Internal_ConsoleTest, HandlePrintableChar_OverwriteMode_CursorAtStart_ShouldOverwriteFirstCharacter)
{
    std::string input = "Hello";
    char newChar = 'Y';
    seedDisplay(input, 0);
    toggleInsertMode(input); // Overwrite mode

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(1);

    handlePrintableChar(newChar, input);

    EXPECT_EQ(input, "Yello");
    EXPECT_EQ(getCursorPos(), 1);
    EXPECT_EQ(getInsertString(), "Hello");
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[1GY");
}

// Test handlePrintableChar with mixed insert and overwrite modes
TEST_F(Internal_ConsoleTest, HandlePrintableChar_MixedModes_ShouldHandleCorrectly)
{
    std::string input = "Hell World";
    char newChar1 = 'o';
    seedDisplay(input, 4); // Between 'Hell' and ' World'

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(2);
    handlePrintableChar(newChar1, input);
    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getCursorPos(), 5);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[5Go World\033[6G");

    // Switch to overwrite mode
    toggleInsertMode(input);
    setCursorPos(6); // Before 'W'
    char newChar2 = 'A';

    handlePrintableChar(newChar2, input);
    EXPECT_EQ(input, "Hello Aorld");
    EXPECT_EQ(getCursorPos(), 7);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[5Go World\033[6G A");
}

// Test handlePrintableChar with multiple overwrites beyond input length
TEST_F(Internal_ConsoleTest, HandlePrintableChar_OverwriteMode_BeyondInputLength_ShouldAppend)
{
    std::string input = "Hello";
    char newChar = '!';
    seedDisplay(input, 5); // End of input
    toggleInsertMode(input); // Overwrite mode

    // Expect the console's print method to be called with '!'
    EXPECT_CALL(*mockConsole, print("!", ::testing::_)).Times(1);

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
    seedDisplay(input, 5); // Between 'Hello' and 'World'

    // One render per inserted character.
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(3);

    // Insert ' ', expect 'Hello World'
    handlePrintableChar(newChar1, input);
    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getCursorPos(), 6);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[6G World\033[7G");

    // Switch to overwrite mode and insert 'C'
    toggleInsertMode(input);
    handlePrintableChar(newChar2, input);
    EXPECT_EQ(input, "Hello Corld");
    EXPECT_EQ(getInsertString(), "Hello World");
    EXPECT_EQ(getCursorPos(), 7);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[6G World\033[7GC");

    // Switch back to insert mode and insert '!'
    toggleInsertMode(input);
    handlePrintableChar(newChar3, input);
    EXPECT_EQ(input, "Hello C!orld");
    EXPECT_EQ(getInsertString(), "");
    EXPECT_EQ(getCursorPos(), 8);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[6G World\033[7GC!orld\033[9G");
}

#pragma endregion
#pragma region GenericSpecialKeys

// Test handleSpecialKey with Enter key
TEST_F(Internal_ConsoleTest, HandleSpecialKey_Enter_ShouldAddToHistoryAndReturnInput)
{
    std::string input = "TestCommand";
    setCursorPos(11); // End of input

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
    seedDisplay(input, 11); // End of input

    // One render erases the last character.
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(1);

    // Call handleSpecialKey with Backspace key
    std::string result = handleSpecialKey('\x7f', input);

    EXPECT_EQ(result, "");
    EXPECT_EQ(input, "Hello Worl");
    EXPECT_EQ(getCursorPos(), 10);
    // Seeks to the dropped 'd', writes a space over it, then steps the cursor
    // back onto that column.
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[11G \033[11G");
}

// Test handleSpecialKey with Backspace key in overwrite mode
TEST_F(Internal_ConsoleTest, HandleSpecialKey_BackspaceInOverwriteMode_ShouldEraseCharacterLeavingBehindPrevious)
{
    std::string input = "Hello World";
    setCursorPos(7); // Right before 'o'
    toggleInsertMode(input); // Toggle overwrite mode

    // New input after character was inserted
    input = "Hello Aorld";
    seedDisplay(input, 7);

    // One render restores the overwritten character.
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(1);

    std::string result = handleSpecialKey('\x7f', input);

    EXPECT_EQ(result, "");
    EXPECT_EQ(input, "Hello World");
    EXPECT_EQ(getInsertString(), "Hello World");
    // Backspace in overwrite mode restores the original 'W' in place.
    EXPECT_EQ(mockConsole->getCapturedOutput(), "\033[7GW\033[7G");
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

    input = "";
    handleEscapeSequence(input, "[A");
    EXPECT_EQ(input, "third command");
    EXPECT_NE(mockConsole->getCapturedOutput().find("third command"), std::string::npos);
    EXPECT_EQ(getCursorPos(), input.size());

    mockConsole->resetCapturedOutput();
    handleEscapeSequence(input, "[A");
    EXPECT_EQ(input, "second command");
    EXPECT_NE(mockConsole->getCapturedOutput().find("second command"), std::string::npos);
    EXPECT_EQ(getCursorPos(), input.size());

    mockConsole->resetCapturedOutput();
    handleEscapeSequence(input, "[A");
    EXPECT_EQ(input, "first command");
    EXPECT_NE(mockConsole->getCapturedOutput().find("first command"), std::string::npos);
    EXPECT_EQ(getCursorPos(), input.size());

    // Already at the oldest entry: nothing changes and nothing is emitted.
    mockConsole->resetCapturedOutput();
    handleEscapeSequence(input, "[A");
    EXPECT_EQ(input, "first command");
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
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

    input = "";
    handleEscapeSequence(input, "[A");
    EXPECT_EQ(input, "third command");

    handleEscapeSequence(input, "[A");
    EXPECT_EQ(input, "second command");
    EXPECT_EQ(getCursorPos(), input.size());

    mockConsole->resetCapturedOutput();
    handleEscapeSequence(input, "[B");
    EXPECT_EQ(input, "third command");
    EXPECT_NE(mockConsole->getCapturedOutput().find("third command"), std::string::npos);
    EXPECT_EQ(getCursorPos(), input.size());

    // Stepping past the newest entry restores the cached (empty) input.
    mockConsole->resetCapturedOutput();
    handleEscapeSequence(input, "[B");
    EXPECT_EQ(input, "");
    EXPECT_EQ(getCursorPos(), 0u);
}

// Test updateDisplayInput after navigating history
TEST_F(Internal_ConsoleTest, UpdateDisplayInput_ShouldUpdateInputDisplayCorrectly)
{
    std::string oldInput = "OldCommand";
    std::string newInput = "NewCommand";

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
