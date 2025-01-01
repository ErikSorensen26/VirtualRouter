#include <gtest/gtest.h>
#include <Terminal.h>

// Declare the test class as a friend to access private members
class TerminalTest : public ::testing::Test
{
protected:
    Terminal terminal{false}; // Initializing Terminal without debug mode

    void SetUp() override
    {
        // Any setup needed for Terminal
    }

    void TearDown() override
    {
        // Any cleanup needed after tests
    }

    std::string expandIPv6Address(std::string& ipv6)
    {
        return terminal.expandIPv6Address(ipv6);
    }
};

// Test: Initialization
TEST_F(TerminalTest, InitializeTerminal)
{
    EXPECT_NO_THROW(terminal.initConsole());
    EXPECT_EQ(terminal.currentMode, terminal.mode.userExec);
}

// Test: Handle IPv6 Input
TEST_F(TerminalTest, HandleIPv6Input)
{

}

// Test: Recover State

