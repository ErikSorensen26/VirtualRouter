#include <gtest/gtest.h>
#include <Terminal.h>
#include <MockConsole.hpp>
#include <MockFileSystem.hpp>

using json = nlohmann::json;

// Declare the test class as a friend to access private members
class TerminalTest : public ::testing::Test
{
protected:
    // Mock objects
    std::shared_ptr<testing::NiceMock<ReducedMockConsole>> mockConsole;
    static std::shared_ptr<testing::NiceMock<MockFileSystem>> mockFileSystem;

    // Shared pointers for dependencies
    static std::shared_ptr<IFileSystem> realFileSystem;

    // File strings
    static std::string commandTreeString;
    static std::string configSchemaString;
    static std::string configFileString;

    Mode mode;

    // Terminal instance
    std::unique_ptr<Terminal> terminal;

    void SetUp() override
    {
        // Create the terminal instance with nexessary dependencies
        mockConsole = std::make_shared<testing::NiceMock<ReducedMockConsole>>();
        
        terminal = std::make_unique<Terminal>(mockConsole, mockFileSystem);
        terminal->initTerminal();
        terminal->changeMode(terminal->mode.globalConfiguration);
        terminal->paginationCount = 0;
    }

    void TearDown() override {
        Global::getInstance().resetInstance();
    }

    static void SetUpTestSuite()
    {
        // Create Initiate file systems
        realFileSystem = std::make_shared<RealFileSystem>();
        mockFileSystem = std::make_shared<testing::NiceMock<MockFileSystem>>();

        // Load the JSON file into the sampleCommandTree
        if (realFileSystem->fileExists(COMMAND_TREE))
        {
            realFileSystem->readFile(COMMAND_TREE, commandTreeString);
        }
        else
        {
            FAIL() << "Failed to open the command tree file: " << COMMAND_TREE;
        }

        // Load the JSON file into the config schema
        if (realFileSystem->fileExists(CONFIG_SCHEMA))
        {
            realFileSystem->readFile(CONFIG_SCHEMA, configSchemaString);
        }
        else
        {
            FAIL() << "Failed to open the config schema file: " << CONFIG_SCHEMA;
        }

        if (realFileSystem->fileExists(CONFIG_FILE))
        {
            realFileSystem->readFile(CONFIG_FILE, configFileString);
        }
        else
        {
            FAIL() << "Failed to open the config schem file: " << CONFIG_FILE;
        }

        mockFileSystem->setupMockFile(COMMAND_TREE, commandTreeString);
        mockFileSystem->setupMockFile(CONFIG_SCHEMA, configSchemaString);
        mockFileSystem->setupMockFile(CONFIG_FILE, configFileString);
        mockFileSystem->setupMockFile(STARTUP_FILE, "{}");
    }
    
    static void TearDownTestSuite() {}

    // Helper functions
    bool executeCommand(std::string& command) {return handleInput(command);}
    bool handleInput(std::string& command) {return terminal->handleInput(command);}
    void changeMode(std::string& newMode) {terminal->changeMode(newMode);}
    void configureRoutingMode(std::string& newMode) {terminal->configureRoutingMode(newMode);}
    std::string getHostname() {return Global::getInstance().getHostname();}
    std::string getCurrentMode() {return terminal->currentMode;}
    std::string getCurrentSubMode() {return terminal->currentSubMode;}
    std::string getNextLine() {return terminal->nextLine;}
    const json& getCommandTree() const {return terminal->commandTree;}
    const json& getWorkingDirectory() const {return terminal->workingDirectory;}
    void autoComplete() {autoComplete();}
};

// Static Initialization
std::shared_ptr<IFileSystem> TerminalTest::realFileSystem = nullptr;
std::shared_ptr<testing::NiceMock<MockFileSystem>> TerminalTest::mockFileSystem = nullptr;
std::string TerminalTest::commandTreeString;
std::string TerminalTest::configSchemaString;
std::string TerminalTest::configFileString;
#pragma region ModeChange
/*

// Test Changing mode from global to user EXEC
TEST_F(TerminalTest, ModeChange_GlobalToUserExec_ShouldUpdateMode) 
{
    // Arrange
    std::string newMode = "#"; // User EXEC mode prompt

    // Act
    changeMode(newMode);

    // Assert
    EXPECT_EQ(getCurrentMode(), newMode);
    EXPECT_EQ(getWorkingDirectory(), getCommandTree()[newMode]);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test Changing mode to invalid mode should fail
TEST_F(TerminalTest, ModeChange_InvalidMode_ShouldRejectModeChange) 
{
    // Arrange
    std::string invalidMode = "invalid_mode";

    std::string expectedPrompt = "(config)#";

    // Act
    changeMode(invalidMode);

    // Assert
    EXPECT_EQ(getCurrentMode(), expectedPrompt); // Assuming initial mode
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test Switching to sub-mode (e.g., interface configuration)
TEST_F(TerminalTest, ModeChange_SwitchToSubMode_ShouldUpdateMode) 
{
    // Arrange
    std::string subMode = "eigrp_classic";

    // Act
    configureRoutingMode(subMode);

    // Assert
    EXPECT_EQ(getCurrentSubMode(), subMode);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

#pragma endregion
#pragma region Testing

// Test Handling empty input should reject command
TEST_F(TerminalTest, InputHandling_EmptyInput_ShouldRejectCommand) 
{
    // Arrange
    std::string command = "\n";

    // Act
    bool result = executeCommand(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 3.2: Handling input with only spaces should reject command
TEST_F(TerminalTest, InputHandling_SpacesOnly_ShouldRejectCommand) 
{
    // Arrange
    std::string command = "   \n";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(7);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#      ");
}

// Test 3.3: Handling valid input with leading and trailing spaces
TEST_F(TerminalTest, InputHandling_ValidInputWithSpaces_ShouldProcessCommand) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string rawCommand = "  hostname Router1  \n";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(41);

    // Act
    bool result = handleInput(rawCommand);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(getHostname(), "Router1");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#    h o s t n a m e   R o u t e r 1     ");
}

#pragma endregion
#pragma region DoTesting

// Test Executing a "do" command from configuration mode
TEST_F(TerminalTest, DoCommand_FromConfigurationMode_ShouldExecutePrivilegedCommand) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string doCommand = "do show running-config\n";

    // Expectation: Terminal executes the "do" command and prints output
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(45);

    // Act
    bool result = handleInput(doCommand);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "Router1(config)#d o   s h o w   r u n n i n g - c o n f i g ");
}

// Test Executing an invalid "do" command
TEST_F(TerminalTest, DoCommand_InvalidCommand_ShouldRejectCommand) {
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string doCommand = "do invalidcmd\n";

    // Expectation: Terminal rejects the "do" command and prints error
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(28);

    // Act
    bool result = handleInput(doCommand);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "Router1(config)#d o   i n v a l i d c m d \n                ^\n% Invlid input detected at '^' marker.\n");
}

// Test Executing a "do" command with missing parameters
TEST_F(TerminalTest, DoCommand_MissingParameters_ShouldRejectCommand) {
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string doCommand = "do ping\n";

    // Expectation: Terminal rejects the "do" command due to missing parameters
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(16);

    // Act
    bool result = handleInput(doCommand);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "Router1(config)#d o   p i n g \nIncomplete Command");
}

// Test Executing a "do" command from sub-mode
TEST_F(TerminalTest, DoCommand_FromSubMode_ShouldExecuteCommandWithinSubMode) 
{
    // Arrange
    // First, enter sub-mode
    changeMode(mode.globalConfiguration);
    std::string enterSubModeCmd = "interface GigabitEthernet 1\n";
    std::string doCommand = "do show interface GigabitEthernet 1";

    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(126);

    // Act: Enter sub-mode
    bool result1 = handleInput(enterSubModeCmd);
    EXPECT_TRUE(result1);

    // Now, execute "do" command within sub-mode

    // Act: Execute "do" command within sub-mode
    bool result2 = handleInput(doCommand);
    EXPECT_TRUE(result2);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "Router1(config)#i n t e r f a c e   G i g a b i t E t h e r n e t   1 Router1(config-if)#d o   s h o w   i n t e r f a c e   G i g a b i t E t h e r n e t   1 ");
}
*/

#pragma endregion
#pragma region AutoComplete

// Test Displaying help using '?'
TEST_F(TerminalTest, HelpRequest_WithQuestionMark_ShouldDisplayAvailableCommands) 
{
    // Arrange
    changeMode(mode.userExec);
    std::string helpCommand = "?";
    std::string expectedOutput;

    // Expectation: Terminal prints available commands and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(15);

    // Act
    bool result = handleInput(helpCommand);

    // Assert
    EXPECT_TRUE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router>?\n  <1-99>          Session number to resume\n  connect         Open a terminal connection\n  disable         Turn off privileged commands\n  disconnect      Disconnect an existing network connection\n  enable          Turn on privileged commands\n  logout          Exit from the EXEC\n  ping            Send echo messages\n  resume          Resume an active network connection\n  show            Show running system information\n  ssh             Open a secure shell client connection\n  telnet          Open a telnet connection\n  terminal        Set terminal line parameters\n  traceroute      Trace route to destination");
}

// Test Auto-completing a unique partial command using Tab
TEST_F(TerminalTest, AutoComplete_UniquePartialCommand_ShouldCompleteCommand) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string partialInput = "host\t";
    std::string finishInput = " Router1\n";

    // Expectation: Terminal auto-completes the command
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(27);

    // Act
    bool result = handleInput(partialInput);
    bool result2 = handleInput(finishInput);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_TRUE(result2);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#h o s t router(config)#hostname   R o u t e r 1 ");
}

// Test Auto-completing an ambiguous partial command using Tab
TEST_F(TerminalTest, AutoComplete_AmbiguousPartialCommand_ShouldListSuggestions) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string partialInput = "a\t";

    // Expectation: Terminal lists available suggestions
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(3);

    // Act
    bool result = handleInput(partialInput);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#a ");
    EXPECT_EQ(getNextLine(), "a ");
}

// Test Auto-completing an exact command should do nothing
TEST_F(TerminalTest, AutoComplete_ExactCommand_ShouldNotChangeInput) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string exactCommand = "exit\t";

    // Expectation: Terminal does not attempt to auto-complete
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(8);

    // Act
    bool result = handleInput(exactCommand);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(getNextLine(), "");
}

// Test Displaying help within sub-mode using '?'
TEST_F(TerminalTest, HelpRequest_InSubMode_ShouldDisplayAvailableSubCommands) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    // First, enter sub-mode
    std::string enterSubModeCmd = "interface GigabitEthernet 1\n";
    std::string helpCommand = "?";

    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(81);

    // Act: Enter sub-mode
    bool result1 = handleInput(enterSubModeCmd);
    EXPECT_TRUE(result1);

    // Act: Invoke help in sub-mode
    bool result2 = handleInput(helpCommand);
    EXPECT_TRUE(result2);
    std::string help = "router(config)#i n t e r f a c e   G i g a b i t E t h e r n e t   1 Router1(config-if)#?\n  arp                    Set arp type (arpa, probe, snap) or timeout\n  bandwidth              Set bandwidth informational parameter\n  cdp                    CDP interface subcommands\n  channel-group          Add this interface to an Etherchannel group\n  crypto                 Encryption/Decryption commands\n  custom-queue-list      Assign a custom queue list to an interface\n  delay                  Specify interface throughput delay\n  description            Interface specific description\n  duplex                 Configure duplex operation.\n  fair-queue             Enable Fair Queuing on an Interface\n  hold-queue             Set hold queue depth\n  ip                     Interface Internet Protocol config \n  ipv6                   IPv6 interface subcommands\n  lldp                   LLDP interface subcommands\n  mac-address            Manually set interface MAC address\n  mtu                    Set the interface Maximum Transmission Unit (MTU)\n  no                     Negate a command or set its defaults\n  pppoe                  pppoe interface subcommands\n  pppoe-client           pppoe client\n  priority-group         Assign a priority group to an interface\n  service-policy         Configure QoS Service Policy\n  shutdown               Shutdown the selected interface\n  speed                  Configure speed operation.\n  standby                HSRP interface configuration commands\n  tx-ring-limit          Configure PA level transmit ring limit";
    EXPECT_EQ(mockConsole->getCapturedOutput(), help);
}

// 6. Command Processing and Validation Tests
#pragma endregion
#pragma region CommandProcessing

/*
// Test 6.1: Processing a valid global command
TEST_F(TerminalTest, CommandProcessing_ValidGlobalCommand_ShouldProcessSuccessfully) {
    // Arrange
    std::string command = "hostname Router1";
    std::string expectedOutput = "hostname Router1\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal prints the command and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(10);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(terminal->getHostname(), "Router1");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 6.2: Processing an invalid global command
TEST_F(TerminalTest, CommandProcessing_InvalidGlobalCommand_ShouldRejectCommand) {
    // Arrange
    std::string command = "invalidcmd";
    std::string expectedError = "Error: Unknown command 'invalidcmd'.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 6.3: Processing a command with missing required arguments
TEST_F(TerminalTest, CommandProcessing_MissingArguments_ShouldRejectCommand) {
    // Arrange
    std::string command = "hostname"; // Missing hostname value
    std::string expectedError = "Error: 'hostname' requires an argument.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(terminal->getHostname(), ""); // Hostname should not be set
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 6.4: Processing a command with excessive arguments
TEST_F(TerminalTest, CommandProcessing_ExcessiveArguments_ShouldRejectCommand) {
    // Arrange
    std::string command = "hostname Router1 ExtraArg";
    std::string expectedError = "Error: 'hostname' command takes exactly one argument.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(terminal->getHostname(), ""); // Hostname should not be set
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 6.5: Processing a volatile command with pattern matching
TEST_F(TerminalTest, CommandProcessing_VolatileCommand_ShouldValidatePatterns) {
    // Arrange
    std::string command = "do ping 192.168.1.1";
    std::string expectedOutput = "Pinging 192.168.1.1...\nSuccess!";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal processes the command and prints output
    EXPECT_CALL(*mockConsole, print(expectedOutput + "\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 6.6: Processing a volatile command with invalid pattern
TEST_F(TerminalTest, CommandProcessing_VolatileCommand_InvalidPattern_ShouldRejectCommand) {
    // Arrange
    std::string command = "do ping 999.999.999.999";
    std::string expectedError = "Error: Invalid IP address '999.999.999.999'.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to invalid IP
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 6.7: Processing a command with special characters
TEST_F(TerminalTest, CommandProcessing_SpecialCharacters_ShouldRejectCommand) {
    // Arrange
    std::string command = "hostname Router@123"; // Assuming '@' is invalid
    std::string expectedError = "Error: Invalid character '@' in hostname.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command and prints error
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(terminal->getHostname(), ""); // Hostname should not be set
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 7. Matching Commands Tests

// Test 7.1: Matching command with exact case
TEST_F(TerminalTest, MatchingCommands_ExactCase_ShouldMatchSuccessfully) {
    // Arrange
    std::string command = "hostname RouterExact";
    std::string expectedOutput = "hostname RouterExact\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal processes the command
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(terminal->getHostname(), "RouterExact");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 7.2: Matching command with different casing (assuming case-insensitive)
TEST_F(TerminalTest, MatchingCommands_DifferentCasing_ShouldMatchSuccessfully) {
    // Arrange
    std::string command = "HoStNaMe RouterCase";
    std::string expectedOutput = "hostname RouterCase\n"; // Assuming normalization to lower case
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal normalizes and processes the command
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(terminal->getHostname(), "RouterCase");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 7.3: Matching partial command to full command
TEST_F(TerminalTest, MatchingCommands_PartialToFull_ShouldMatchSuccessfully) {
    // Arrange
    std::string partialCommand = "host RouterPartial";
    std::string normalizedCommand = "hostname RouterPartial";
    std::string expectedOutput = normalizedCommand + "\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(partialCommand);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(terminal->getHostname(), "RouterPartial");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 7.4: Matching command with invalid hierarchy
TEST_F(TerminalTest, MatchingCommands_InvalidHierarchy_ShouldRejectCommand) {
    // Arrange
    std::string command = "interface GigabitEthernet0/1 ip address 10.0.0.1 255.255.255.0 extraArg";
    std::string expectedError = "Error: 'ip address' command takes exactly two arguments.\n";
    std::string expectedPrompt = "(interface)# ";

    // Expectation: Terminal rejects the command due to excessive arguments
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 8. Normalization Tests

// Test 8.1: Normalizing a partial command by filling in required words
TEST_F(TerminalTest, Normalization_FillInRequiredWords_ShouldNormalizeCommand) {
    // Arrange
    std::string partialCommand = "host Router1";
    std::string normalizedCommand = "hostname Router1";
    std::string expectedOutput = normalizedCommand + "\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(partialCommand);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(terminal->getHostname(), "Router1");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 8.2: Normalizing command with abbreviated subcommands
TEST_F(TerminalTest, Normalization_AbbreviatedSubcommands_ShouldNormalizeCommand) {
    // Arrange
    std::string command = "int Gig0/1 ip a 10.0.0.1 255.255.255.0";
    std::string normalizedCommand = "interface GigabitEthernet0/1 ip address 10.0.0.1 255.255.255.0";
    std::string expectedOutput = normalizedCommand + "\n";
    std::string expectedPrompt = "(interface)# ";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    // Additional assertions can be added based on internal state
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 8.3: Normalizing command with mixed case and spaces
TEST_F(TerminalTest, Normalization_MixedCaseAndSpaces_ShouldNormalizeCommand) {
    // Arrange
    std::string command = "  HoStNaMe   RouterMixedCase  ";
    std::string normalizedCommand = "hostname RouterMixedCase";
    std::string expectedOutput = normalizedCommand + "\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(terminal->getHostname(), "RouterMixedCase");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 9. LINE Commands Tests

// Test 9.1: Configuring LINE VTY commands
TEST_F(TerminalTest, LineCommand_ConfigureVTY_ShouldProcessSuccessfully) {
    // Arrange
    std::string command = "line vty 0 4";
    std::string expectedOutput = "line vty 0 4\n";
    std::string expectedPrompt = "(line)# ";

    // Expectation: Terminal processes the LINE VTY command and changes prompt
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_TRUE(terminal->isLineConfigured(0, 4));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 9.2: Configuring LINE console command
TEST_F(TerminalTest, LineCommand_ConfigureConsole_ShouldProcessSuccessfully) {
    // Arrange
    std::string command = "line console 0";
    std::string expectedOutput = "line console 0\n";
    std::string expectedPrompt = "(line)# ";

    // Expectation: Terminal processes the LINE console command and changes prompt
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_TRUE(terminal->isLineConfigured(0, "console"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 9.3: Exiting LINE mode back to global mode
TEST_F(TerminalTest, LineCommand_ExitSubMode_ShouldReturnToGlobalMode) {
    // Arrange
    std::string enterLineModeCmd = "line vty 0 4";
    std::string enterLineModeOutput = "line vty 0 4\n";
    std::string lineModePrompt = "(line)# ";

    std::string exitLineModeCmd = "exit";
    std::string exitLineModeOutput = "exit\n";
    std::string globalPrompt = "(config)# ";

    // Expectation: Enter LINE mode
    EXPECT_CALL(*mockConsole, print(enterLineModeOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(lineModePrompt))
        .Times(1);

    // Act: Enter LINE mode
    bool result1 = handleInput(enterLineModeCmd);
    EXPECT_TRUE(result1);

    // Expectation: Exit LINE mode
    EXPECT_CALL(*mockConsole, print(exitLineModeOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(globalPrompt))
        .Times(1);

    // Act: Exit LINE mode
    bool result2 = handleInput(exitLineModeCmd);
    EXPECT_TRUE(result2);

    // Assert
    EXPECT_EQ(terminal->getCurrentMode(), "(config)# ");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 10. Global Commands Tests

// Test 10.1: Executing global command 'exit' to leave configuration mode
TEST_F(TerminalTest, GlobalCommand_ExitConfigurationMode_ShouldChangeMode) {
    // Arrange
    std::string command = "exit";
    std::string expectedOutput = "exit\n";
    std::string expectedPrompt = "> "; // Assuming '>' is user EXEC mode

    // Expectation: Terminal processes the 'exit' command and changes mode
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(terminal->getCurrentMode(), "> ");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 10.2: Executing global command 'end' to exit to privileged EXEC mode
TEST_F(TerminalTest, GlobalCommand_EndConfigurationMode_ShouldChangeMode) {
    // Arrange
    std::string command = "end";
    std::string expectedOutput = "end\n";
    std::string expectedPrompt = "# "; // Privileged EXEC mode

    // Expectation: Terminal processes the 'end' command and changes mode
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(terminal->getCurrentMode(), "# ");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 10.3: Executing global command 'help' to display available global commands
TEST_F(TerminalTest, GlobalCommand_Help_ShouldDisplayAvailableGlobalCommands) {
    // Arrange
    std::string command = "help";
    std::vector<std::string> availableCommands = {"hostname", "interface", "exit", "end", "do"};
    std::string expectedOutput;
    for (const auto& cmd : availableCommands) {
        expectedOutput += cmd + "\n";
    }
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal prints available global commands and prompt
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 10.4: Executing global command 'do' without subcommand should reject
TEST_F(TerminalTest, GlobalCommand_DoWithoutSubcommand_ShouldRejectCommand) {
    // Arrange
    std::string command = "do";
    std::string expectedError = "Error: 'do' requires a subcommand.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command and prints error
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 11. Invalid Inputs Tests

// Test 11.1: Processing a completely unknown command
TEST_F(TerminalTest, InvalidInput_UnknownCommand_ShouldRejectCommand) {
    // Arrange
    std::string command = "foobar";
    std::string expectedError = "Error: Unknown command 'foobar'.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command and prints error
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 11.2: Processing a command with invalid syntax
TEST_F(TerminalTest, InvalidInput_InvalidSyntax_ShouldRejectCommand) {
    // Arrange
    std::string command = "interface GigabitEthernet0/1 ip address";
    std::string expectedError = "Error: 'ip address' command requires two arguments.\n";
    std::string expectedPrompt = "(interface)# ";

    // Expectation: Terminal rejects the command due to missing arguments
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 11.3: Processing a command with invalid characters
TEST_F(TerminalTest, InvalidInput_InvalidCharacters_ShouldRejectCommand) {
    // Arrange
    std::string command = "hostname Router!@#";
    std::string expectedError = "Error: Invalid characters in hostname.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to invalid characters
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 11.4: Processing a command with invalid mode in hierarchy
TEST_F(TerminalTest, InvalidInput_InvalidModeHierarchy_ShouldRejectCommand) {
    // Arrange
    std::string command = "router ospf 1 area 0";
    std::string expectedError = "Error: 'router ospf' command not allowed in current mode.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to invalid mode hierarchy
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 12. Pattern Matching for Volatile Commands Tests

// Test 12.1: Processing a volatile command with valid IP address
TEST_F(TerminalTest, PatternMatching_VolatileCommand_ValidIPAddress_ShouldProcessSuccessfully) {
    // Arrange
    std::string command = "do ping 192.168.1.1";
    std::string expectedOutput = "Pinging 192.168.1.1...\nSuccess!";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal processes the command and prints output
    EXPECT_CALL(*mockConsole, print(expectedOutput + "\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 12.2: Processing a volatile command with invalid IP address
TEST_F(TerminalTest, PatternMatching_VolatileCommand_InvalidIPAddress_ShouldRejectCommand) {
    // Arrange
    std::string command = "do ping 999.999.999.999";
    std::string expectedError = "Error: Invalid IP address '999.999.999.999'.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to invalid IP
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 12.3: Processing a volatile command with missing parameters
TEST_F(TerminalTest, PatternMatching_VolatileCommand_MissingParameters_ShouldRejectCommand) {
    // Arrange
    std::string command = "do traceroute";
    std::string expectedError = "Error: 'traceroute' requires a destination address.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to missing parameters
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 13. IPv6 Expanding Tests

// Test 13.1: Expanding a compressed IPv6 address
TEST_F(TerminalTest, IPv6Expanding_CompressedAddress_ShouldExpandCorrectly) {
    // Arrange
    std::string compressedIPv6 = "2001:db8::1";
    std::string expectedExpanded = "2001:0db8:0000:0000:0000:0000:0000:0001";

    // Act
    std::string expanded = terminal->expandIPv6Address(compressedIPv6);

    // Assert
    EXPECT_EQ(expanded, expectedExpanded);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 13.2: Expanding a fully expanded IPv6 address should remain unchanged
TEST_F(TerminalTest, IPv6Expanding_FullyExpandedAddress_ShouldRemainUnchanged) {
    // Arrange
    std::string expandedIPv6 = "2001:0db8:85a3:0000:0000:8a2e:0370:7334";
    std::string expectedExpanded = "2001:0db8:85a3:0000:0000:8a2e:0370:7334";

    // Act
    std::string result = terminal->expandIPv6Address(expandedIPv6);

    // Assert
    EXPECT_EQ(result, expectedExpanded);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 13.3: Expanding an IPv6 address with multiple "::" should handle error
TEST_F(TerminalTest, IPv6Expanding_MultipleCompressedSections_ShouldHandleError) {
    // Arrange
    std::string compressedIPv6 = "2001::85a3::7334";
    std::string expectedError = "Error: Invalid IPv6 address format.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the invalid IPv6 address
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    std::string expanded = terminal->expandIPv6Address(compressedIPv6);

    // Assert
    // Assuming the method returns an empty string on error
    EXPECT_EQ(expanded, "");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 14. Sub Modes Tests

// Test 14.1: Entering and configuring routing protocol sub-mode (OSPF)
TEST_F(TerminalTest, SubMode_RoutingProtocol_OSPF_ShouldConfigureSuccessfully) {
    // Arrange
    std::string enterSubModeCmd = "router ospf 1";
    std::string enterSubModeOutput = "router ospf 1\n";
    std::string subModePrompt = "(router-ospf)# ";

    std::string configureCmd = "network 10.0.0.0 0.0.0.255 area 0";
    std::string configureOutput = "network 10.0.0.0 0.0.0.255 area 0\n";
    std::string expectedPrompt = "(router-ospf)# ";

    // Expectation: Enter sub-mode
    EXPECT_CALL(*mockConsole, print(enterSubModeOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(subModePrompt))
        .Times(1);

    // Act: Enter sub-mode
    bool result1 = handleInput(enterSubModeCmd);
    EXPECT_TRUE(result1);

    // Expectation: Configure routing protocol
    EXPECT_CALL(*mockConsole, print(configureOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act: Configure routing protocol
    bool result2 = handleInput(configureCmd);
    EXPECT_TRUE(result2);

    // Assert
    EXPECT_TRUE(terminal->isRoutingProtocolConfigured("OSPF", 1));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 14.2: Exiting sub-mode and returning to global mode
TEST_F(TerminalTest, SubMode_ExitSubMode_ShouldReturnToGlobalMode) {
    // Arrange
    std::string enterSubModeCmd = "router rip";
    std::string enterSubModeOutput = "router rip\n";
    std::string subModePrompt = "(router-rip)# ";

    std::string exitSubModeCmd = "exit";
    std::string exitSubModeOutput = "exit\n";
    std::string globalPrompt = "(config)# ";

    // Expectation: Enter sub-mode
    EXPECT_CALL(*mockConsole, print(enterSubModeOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(subModePrompt))
        .Times(1);

    // Act: Enter sub-mode
    bool result1 = handleInput(enterSubModeCmd);
    EXPECT_TRUE(result1);

    // Expectation: Exit sub-mode
    EXPECT_CALL(*mockConsole, print(exitSubModeOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(globalPrompt))
        .Times(1);

    // Act: Exit sub-mode
    bool result2 = handleInput(exitSubModeCmd);
    EXPECT_TRUE(result2);

    // Assert
    EXPECT_EQ(terminal->getCurrentMode(), "(config)# ");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 14.3: Configuring CTR (Control) sub-mode
TEST_F(TerminalTest, SubMode_CTR_ShouldConfigureSuccessfully) {
    // Arrange
    std::string enterSubModeCmd = "ctr config";
    std::string enterSubModeOutput = "ctr config\n";
    std::string subModePrompt = "(ctr)# ";

    std::string configureCmd = "enable feature xyz";
    std::string configureOutput = "enable feature xyz\n";
    std::string expectedPrompt = "(ctr)# ";

    // Expectation: Enter sub-mode
    EXPECT_CALL(*mockConsole, print(enterSubModeOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(subModePrompt))
        .Times(1);

    // Act: Enter sub-mode
    bool result1 = handleInput(enterSubModeCmd);
    EXPECT_TRUE(result1);

    // Expectation: Configure CTR
    EXPECT_CALL(*mockConsole, print(configureOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act: Configure CTR
    bool result2 = handleInput(configureCmd);
    EXPECT_TRUE(result2);

    // Assert
    EXPECT_TRUE(terminal->isCtrFeatureEnabled("xyz"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 15. Batch Processing Tests

// Utility function to simulate batch processing and recovery
bool BatchProcessAndRecover(Terminal& terminal, const std::vector<std::string>& commands,
                            const std::vector<std::string>& expectedOutputs,
                            std::vector<std::string>& recoveredCommands) {
    // Execute commands
    for (size_t i = 0; i < commands.size(); ++i) {
        const std::string& cmd = commands[i];
        const std::string& expectedOutput = expectedOutputs[i];
        if (cmd.find("save config") != std::string::npos) {
            EXPECT_CALL(*terminal.mockConsole, print("Configuration saved successfully.\n"))
                .Times(1);
            EXPECT_CALL(*terminal.mockFileSystem, writeFile("../configs.json", _))
                .Times(1)
                .WillOnce(Return(true));
        }
        else {
            std::string output = cmd + "\n";
            std::string prompt = (cmd.find("interface") != std::string::npos) ? "(interface)# " : "(config)# ";
            EXPECT_CALL(*terminal.mockConsole, print(output))
                .Times(1);
            EXPECT_CALL(*terminal.mockConsole, print(prompt))
                .Times(1);
        }

        bool result = terminal.executeCommand(cmd);
        if (cmd.find("save config") == std::string::npos) {
            EXPECT_TRUE(result);
        }
    }

    // Mock reading the saved configuration
    json savedConfig;
    savedConfig["commands"] = std::vector<std::string>(commands.begin(), commands.end() - 1); // Exclude 'save config'

    EXPECT_CALL(*terminal.mockFileSystem, readFile("../configs.json", _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string& path, std::string& content) -> bool {
            content = savedConfig.dump();
            return true;
        }));

    // Expectation: Terminal executes recovered commands
    for (const auto& cmd : savedConfig["commands"]) {
        std::string output = cmd + "\n";
        std::string prompt = (cmd.find("interface") != std::string::npos) ? "(interface)# " : "(config)# ";
        EXPECT_CALL(*terminal.mockConsole, print(output))
            .Times(1);
        EXPECT_CALL(*terminal.mockConsole, print(prompt))
            .Times(1);
        recoveredCommands.push_back(cmd);
    }

    // Recover state
    bool recoveryResult = terminal.recoverState();
    return recoveryResult;
}

// Test 15.1: Batch processing multiple configuration commands and recovering them accurately
TEST_F(TerminalTest, BatchProcessing_MultipleCommands_ShouldProcessAndRecoverAccurately) {
    // Arrange
    std::vector<std::string> commands = {
        "hostname BatchRouter",
        "interface GigabitEthernet0/1",
        "ip address 172.16.0.1 255.255.255.0",
        "exit",
        "do show running-config",
        "save config"
    };
    std::vector<std::string> expectedOutputs = {
        "hostname BatchRouter\n",
        "interface GigabitEthernet0/1\n",
        "ip address 172.16.0.1 255.255.255.0\n",
        "exit\n",
        "Running Configuration:\n...",
        "Configuration saved successfully.\n"
    };
    std::vector<std::string> recoveredCommands;

    // Act & Assert
    bool recoveryResult = BatchProcessAndRecover(*terminal, commands, expectedOutputs, recoveredCommands);
    EXPECT_TRUE(recoveryResult);
    EXPECT_EQ(recoveredCommands.size(), commands.size() - 1); // Exclude 'save config'

    // Verify recovered commands match input
    for (size_t i = 0; i < recoveredCommands.size(); ++i) {
        EXPECT_EQ(recoveredCommands[i], commands[i]);
    }

    // Additional assertions based on internal state
    EXPECT_EQ(terminal->getHostname(), "BatchRouter");
    EXPECT_TRUE(terminal->isInterfaceConfigured("GigabitEthernet0/1"));
    EXPECT_TRUE(terminal->isIpAddressAssigned("GigabitEthernet0/1", "172.16.0.1", "255.255.255.0"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 15.2: Batch processing with invalid commands should handle errors and continue
TEST_F(TerminalTest, BatchProcessing_InvalidCommands_ShouldHandleErrorsAndContinue) {
    // Arrange
    std::vector<std::string> commands = {
        "hostname BatchRouter",
        "invalidcmd",
        "interface GigabitEthernet0/2",
        "ip address 10.0.0.1 255.255.255.0",
        "save config"
    };
    std::vector<std::string> expectedOutputs = {
        "hostname BatchRouter\n",
        "Error: Unknown command 'invalidcmd'.\n",
        "interface GigabitEthernet0/2\n",
        "ip address 10.0.0.1 255.255.255.0\n",
        "Configuration saved successfully.\n"
    };
    std::vector<std::string> recoveredCommands;

    // Expectation: Terminal should handle 'invalidcmd' and continue processing
    EXPECT_CALL(*mockConsole, print("invalidcmd\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print("Error: Unknown command 'invalidcmd'.\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print("(config)# "))
        .Times(1);

    // Act
    for (size_t i = 0; i < commands.size(); ++i) {
        const std::string& cmd = commands[i];
        const std::string& expectedOutput = expectedOutputs[i];
        if (cmd.find("save config") != std::string::npos) {
            // Expect save operation
            EXPECT_CALL(*mockConsole, print("Configuration saved successfully.\n"))
                .Times(1);
            EXPECT_CALL(*mockFileSystem, writeFile("../configs.json", _))
                .Times(1)
                .WillOnce(Return(true));
        }
        else if (cmd.find("invalidcmd") != std::string::npos) {
            // Already handled above
            continue;
        }
        else {
            // Regular command execution
            std::string output = cmd + "\n";
            std::string prompt = (cmd.find("interface") != std::string::npos) ? "(interface)# " : "(config)# ";
            EXPECT_CALL(*mockConsole, print(output))
                .Times(1);
            EXPECT_CALL(*mockConsole, print(prompt))
                .Times(1);
        }

        bool result = handleInput(cmd);
        if (cmd.find("invalidcmd") == std::string::npos) {
            EXPECT_TRUE(result);
        }
        else {
            EXPECT_FALSE(result);
        }
    }

    // Mock the readFile to return the saved configuration
    json savedConfig;
    savedConfig["commands"] = {"hostname BatchRouter", "interface GigabitEthernet0/2", "ip address 10.0.0.1 255.255.255.0"};

    EXPECT_CALL(*mockFileSystem, readFile("../configs.json", _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string& path, std::string& content) -> bool {
            content = savedConfig.dump();
            return true;
        }));

    // Expectation: Terminal executes the recovered commands
    for (const auto& cmd : savedConfig["commands"]) {
        std::string output = cmd + "\n";
        std::string prompt = (cmd.find("interface") != std::string::npos) ? "(interface)# " : "(config)# ";
        EXPECT_CALL(*mockConsole, print(output))
            .Times(1);
        EXPECT_CALL(*mockConsole, print(prompt))
            .Times(1);
        recoveredCommands.push_back(cmd);
    }

    // Act: Recover state
    bool recoveryResult = terminal->recoverState();

    // Assert
    EXPECT_TRUE(recoveryResult);
    EXPECT_EQ(recoveredCommands.size(), commands.size() - 2); // Exclude 'save config' and 'invalidcmd'

    // Verify recovered commands match input (excluding invalid)
    for (size_t i = 0; i < recoveredCommands.size(); ++i) {
        EXPECT_EQ(recoveredCommands[i], commands[i < 2 ? i : i + 1]); // Adjust index to skip 'invalidcmd'
    }

    // Additional assertions based on internal state
    EXPECT_EQ(terminal->getHostname(), "BatchRouter");
    EXPECT_TRUE(terminal->isInterfaceConfigured("GigabitEthernet0/2"));
    EXPECT_TRUE(terminal->isIpAddressAssigned("GigabitEthernet0/2", "10.0.0.1", "255.255.255.0"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 16. Invalid Inputs and Error Handling Tests

// Test 16.1: Processing a command with invalid command hierarchy
TEST_F(TerminalTest, InvalidInput_InvalidCommandHierarchy_ShouldRejectCommand) {
    // Arrange
    std::string command = "interface GigabitEthernet0/1 router ospf 1"; // Assuming incorrect hierarchy
    std::string expectedError = "Error: 'router ospf' command not allowed in current mode.\n";
    std::string expectedPrompt = "(interface)# ";

    // Expectation: Terminal rejects the command and prints error
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 16.2: Processing a command with invalid interface name
TEST_F(TerminalTest, InvalidInput_InvalidInterfaceName_ShouldRejectCommand) {
    // Arrange
    std::string command = "interface GigabitEthernet0/999";
    std::string expectedError = "Error: Interface 'GigabitEthernet0/999' does not exist.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command and prints error
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 16.3: Processing a command with invalid routing protocol identifier
TEST_F(TerminalTest, InvalidInput_InvalidRoutingProtocol_ShouldRejectCommand) {
    // Arrange
    std::string command = "router invalidproto 1";
    std::string expectedError = "Error: Unknown routing protocol 'invalidproto'.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command and prints error
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 17. Comprehensive Configuration Testing

// Test 17.1: Executing a comprehensive list of valid commands and verifying state
TEST_F(TerminalTest, ComprehensiveConfiguration_ValidCommands_ShouldUpdateStateCorrectly) {
    // Arrange
    std::vector<std::string> commands = {
        "hostname ComprehensiveRouter",
        "interface GigabitEthernet0/1",
        "ip address 192.168.1.1 255.255.255.0",
        "description Uplink Interface",
        "no shutdown",
        "exit",
        "router ospf 1",
        "network 192.168.1.0 0.0.0.255 area 0",
        "exit",
        "save config"
    };
    std::vector<std::string> expectedOutputs = {
        "hostname ComprehensiveRouter\n",
        "interface GigabitEthernet0/1\n",
        "ip address 192.168.1.1 255.255.255.0\n",
        "description Uplink Interface\n",
        "no shutdown\n",
        "exit\n",
        "router ospf 1\n",
        "network 192.168.1.0 0.0.0.255 area 0\n",
        "exit\n",
        "Configuration saved successfully.\n"
    };
    std::vector<std::string> recoveredCommands;

    // Act: Execute commands
    for (size_t i = 0; i < commands.size(); ++i) {
        const std::string& cmd = commands[i];
        const std::string& expectedOutput = expectedOutputs[i];
        if (cmd.find("save config") != std::string::npos) {
            // Expect save operation
            EXPECT_CALL(*mockConsole, print("Configuration saved successfully.\n"))
                .Times(1);
            EXPECT_CALL(*mockFileSystem, writeFile("../configs.json", _))
                .Times(1)
                .WillOnce(Return(true));
        }
        else {
            // Regular command execution
            std::string output = cmd + "\n";
            std::string prompt;
            if (cmd.find("interface") != std::string::npos) {
                prompt = "(interface)# ";
            }
            else if (cmd.find("router") != std::string::npos) {
                prompt = "(router-ospf)# ";
            }
            else {
                prompt = "(config)# ";
            }
            EXPECT_CALL(*mockConsole, print(expectedOutput))
                .Times(1);
            EXPECT_CALL(*mockConsole, print(prompt))
                .Times(1);
        }

        bool result = handleInput(cmd);
        EXPECT_TRUE(result);
    }

    // Mock the readFile to return the saved configuration
    json savedConfig;
    savedConfig["commands"] = std::vector<std::string>(commands.begin(), commands.end() - 1); // Exclude 'save config'

    EXPECT_CALL(*mockFileSystem, readFile("../configs.json", _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string& path, std::string& content) -> bool {
            content = savedConfig.dump();
            return true;
        }));

    // Expectation: Terminal executes the recovered commands
    for (const auto& cmd : savedConfig["commands"]) {
        std::string output = cmd + "\n";
        std::string prompt;
        if (cmd.find("interface") != std::string::npos) {
            prompt = "(interface)# ";
        }
        else if (cmd.find("router") != std::string::npos) {
            prompt = "(router-ospf)# ";
        }
        else {
            prompt = "(config)# ";
        }
        EXPECT_CALL(*mockConsole, print(output))
            .Times(1);
        EXPECT_CALL(*mockConsole, print(prompt))
            .Times(1);
        recoveredCommands.push_back(cmd);
    }

    // Act: Recover state
    bool recoveryResult = terminal->recoverState();

    // Assert
    EXPECT_TRUE(recoveryResult);
    EXPECT_EQ(terminal->getHostname(), "ComprehensiveRouter");
    EXPECT_TRUE(terminal->isInterfaceConfigured("GigabitEthernet0/1"));
    EXPECT_TRUE(terminal->isIpAddressAssigned("GigabitEthernet0/1", "192.168.1.1", "255.255.255.0"));
    EXPECT_TRUE(terminal->isInterfaceDescriptionSet("GigabitEthernet0/1", "Uplink Interface"));
    EXPECT_TRUE(terminal->isInterfaceShutdown("GigabitEthernet0/1") == false); // 'no shutdown' was executed
    EXPECT_TRUE(terminal->isRoutingProtocolConfigured("OSPF", 1));
    EXPECT_TRUE(terminal->isOspfNetworkConfigured("192.168.1.0", "0.0.0.255", "area 0"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 18. Additional Edge Case Tests

// Test 18.1: Processing a command with multiple spaces and tabs
TEST_F(TerminalTest, EdgeCase_MultipleSpacesAndTabs_ShouldNormalizeAndProcessSuccessfully) {
    // Arrange
    std::string command = "  hostname\t\t\t RouterEdgeCase  ";
    std::string normalizedCommand = "hostname RouterEdgeCase";
    std::string expectedOutput = normalizedCommand + "\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(terminal->getHostname(), "RouterEdgeCase");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 18.2: Processing a command with newline characters
TEST_F(TerminalTest, EdgeCase_CommandWithNewlines_ShouldHandleGracefully) {
    // Arrange
    std::string command = "hostname RouterWith\nNewline";
    std::string expectedError = "Error: Invalid command format.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to invalid format
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(terminal->getHostname(), ""); // Hostname should not be set
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 18.3: Processing a command with escape characters
TEST_F(TerminalTest, EdgeCase_CommandWithEscapeCharacters_ShouldHandleGracefully) {
    // Arrange
    std::string command = "hostname Router\033[31mRed";
    std::string expectedError = "Error: Invalid characters in hostname.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to escape characters
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(terminal->getHostname(), ""); // Hostname should not be set
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 18.4: Processing a command with non-ASCII characters
TEST_F(TerminalTest, EdgeCase_CommandWithNonASCIICharacters_ShouldRejectCommand) {
    // Arrange
    std::string command = "hostname RouterÜñîçødê";
    std::string expectedError = "Error: Invalid characters in hostname.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to non-ASCII characters
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(terminal->getHostname(), ""); // Hostname should not be set
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 18.5: Processing a command with excessive command length
TEST_F(TerminalTest, EdgeCase_CommandWithExcessiveLength_ShouldRejectCommand) {
    // Arrange
    std::string command = "hostname " + std::string(1000, 'A'); // 'hostname ' + 1000 'A's
    std::string expectedError = "Error: Command length exceeds maximum allowed limit.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to excessive length
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(terminal->getHostname(), ""); // Hostname should not be set
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 19. Command Validation Tests

// Test 19.1: Validating a command exists in commands.json
TEST_F(TerminalTest, CommandValidation_ExistingCommand_ShouldValidateSuccessfully) {
    // Arrange
    std::string command = "hostname ValidRouter";
    std::string expectedOutput = "hostname ValidRouter\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal validates and processes the command
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 19.2: Validating a command does not exist in commands.json
TEST_F(TerminalTest, CommandValidation_NonExistingCommand_ShouldRejectCommand) {
    // Arrange
    std::string command = "invalidcommand ValidParam";
    std::string expectedError = "Error: Command 'invalidcommand' not recognized.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 19.3: Validating a subcommand exists within a command in commands.json
TEST_F(TerminalTest, CommandValidation_ExistingSubCommand_ShouldValidateSuccessfully) {
    // Arrange
    std::string command = "interface GigabitEthernet0/1";
    std::string expectedOutput = "interface GigabitEthernet0/1\n";
    std::string expectedPrompt = "(interface)# ";

    // Expectation: Terminal validates and processes the subcommand
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 19.4: Validating a subcommand does not exist within a command in commands.json
TEST_F(TerminalTest, CommandValidation_NonExistingSubCommand_ShouldRejectCommand) {
    // Arrange
    std::string command = "interface GigabitEthernet0/1 invalidsubcmd";
    std::string expectedError = "Error: 'invalidsubcmd' is not a valid subcommand for 'interface'.\n";
    std::string expectedPrompt = "(interface)# ";

    // Expectation: Terminal rejects the subcommand
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 19.5: Validating a command with optional subcommands
TEST_F(TerminalTest, CommandValidation_CommandWithOptionalSubcommands_ShouldValidateSuccessfully) {
    // Arrange
    std::string command = "router ospf 1";
    std::string expectedOutput = "router ospf 1\n";
    std::string expectedPrompt = "(router-ospf)# ";

    // Expectation: Terminal processes the command with optional subcommands
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 20. Additional Tests for Comprehensive Coverage

// Test 20.1: Recovering state after a series of commands
TEST_F(TerminalTest, StateRecovery_AfterSeriesOfCommands_ShouldRestoreCorrectly) {
    // Arrange
    std::vector<std::string> commands = {
        "hostname RecoverRouter",
        "interface GigabitEthernet0/1",
        "ip address 10.0.0.1 255.255.255.0",
        "description Uplink Interface",
        "exit",
        "router ospf 1",
        "network 10.0.0.0 0.0.0.255 area 0",
        "exit",
        "save config"
    };
    std::vector<std::string> expectedOutputs = {
        "hostname RecoverRouter\n",
        "interface GigabitEthernet0/1\n",
        "ip address 10.0.0.1 255.255.255.0\n",
        "description Uplink Interface\n",
        "exit\n",
        "router ospf 1\n",
        "network 10.0.0.0 0.0.0.255 area 0\n",
        "exit\n",
        "Configuration saved successfully.\n"
    };
    std::vector<std::string> recoveredCommands;

    // Act: Execute commands
    for (size_t i = 0; i < commands.size(); ++i) {
        const std::string& cmd = commands[i];
        const std::string& expectedOutput = expectedOutputs[i];
        if (cmd.find("save config") != std::string::npos) {
            // Expect save operation
            EXPECT_CALL(*mockConsole, print("Configuration saved successfully.\n"))
                .Times(1);
            EXPECT_CALL(*mockFileSystem, writeFile("../configs.json", _))
                .Times(1)
                .WillOnce(Return(true));
        }
        else {
            // Regular command execution
            std::string output = cmd + "\n";
            std::string prompt;
            if (cmd.find("interface") != std::string::npos) {
                prompt = "(interface)# ";
            }
            else if (cmd.find("router ospf") != std::string::npos) {
                prompt = "(router-ospf)# ";
            }
            else {
                prompt = "(config)# ";
            }
            EXPECT_CALL(*mockConsole, print(expectedOutput))
                .Times(1);
            EXPECT_CALL(*mockConsole, print(prompt))
                .Times(1);
        }

        bool result = handleInput(cmd);
        if (cmd.find("save config") == std::string::npos) {
            EXPECT_TRUE(result);
        }
    }

    // Mock the readFile to return the saved configuration
    json savedConfig;
    savedConfig["commands"] = std::vector<std::string>(commands.begin(), commands.end() - 1); // Exclude 'save config'

    EXPECT_CALL(*mockFileSystem, readFile("../configs.json", _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string& path, std::string& content) -> bool {
            content = savedConfig.dump();
            return true;
        }));

    // Expectation: Terminal executes the recovered commands
    for (const auto& cmd : savedConfig["commands"]) {
        std::string output = cmd + "\n";
        std::string prompt;
        if (cmd.find("interface") != std::string::npos) {
            prompt = "(interface)# ";
        }
        else if (cmd.find("router ospf") != std::string::npos) {
            prompt = "(router-ospf)# ";
        }
        else {
            prompt = "(config)# ";
        }
        EXPECT_CALL(*mockConsole, print(output))
            .Times(1);
        EXPECT_CALL(*mockConsole, print(prompt))
            .Times(1);
        recoveredCommands.push_back(cmd);
    }

    // Act: Recover state
    bool recoveryResult = terminal->recoverState();

    // Assert
    EXPECT_TRUE(recoveryResult);
    EXPECT_EQ(terminal->getHostname(), "RecoverRouter");
    EXPECT_TRUE(terminal->isInterfaceConfigured("GigabitEthernet0/1"));
    EXPECT_TRUE(terminal->isIpAddressAssigned("GigabitEthernet0/1", "10.0.0.1", "255.255.255.0"));
    EXPECT_TRUE(terminal->isInterfaceDescriptionSet("GigabitEthernet0/1", "Uplink Interface"));
    EXPECT_TRUE(terminal->isRoutingProtocolConfigured("OSPF", 1));
    EXPECT_TRUE(terminal->isOspfNetworkConfigured("10.0.0.0", "0.0.0.255", "area 0"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 20.2: Batch processing with mixed valid and invalid commands
TEST_F(TerminalTest, BatchProcessing_MixedValidAndInvalidCommands_ShouldHandleAppropriately) {
    // Arrange
    std::vector<std::string> commands = {
        "hostname MixedRouter",
        "interface GigabitEthernet0/1",
        "ip address 10.0.0.1 255.255.255.0",
        "invalidcmd",
        "description Main Interface",
        "exit",
        "router rip",
        "network 10.0.0.0",
        "exit",
        "save config"
    };
    std::vector<std::string> expectedOutputs = {
        "hostname MixedRouter\n",
        "interface GigabitEthernet0/1\n",
        "ip address 10.0.0.1 255.255.255.0\n",
        "invalidcmd\n",
        "Error: Unknown command 'invalidcmd'.\n",
        "description Main Interface\n",
        "exit\n",
        "router rip\n",
        "network 10.0.0.0\n",
        "exit\n",
        "Configuration saved successfully.\n"
    };
    std::vector<std::string> recoveredCommands;

    // Act: Execute commands
    for (size_t i = 0; i < commands.size(); ++i) {
        const std::string& cmd = commands[i];
        const std::string& expectedOutput = expectedOutputs[i];
        if (cmd.find("save config") != std::string::npos) {
            // Expect save operation
            EXPECT_CALL(*mockConsole, print("Configuration saved successfully.\n"))
                .Times(1);
            EXPECT_CALL(*mockFileSystem, writeFile("../configs.json", _))
                .Times(1)
                .WillOnce(Return(true));
        }
        else if (cmd.find("invalidcmd") != std::string::npos) {
            // Expect error message
            EXPECT_CALL(*mockConsole, print("invalidcmd\n"))
                .Times(1);
            EXPECT_CALL(*mockConsole, print("Error: Unknown command 'invalidcmd'.\n"))
                .Times(1);
            EXPECT_CALL(*mockConsole, print("(config)# "))
                .Times(1);
        }
        else {
            // Regular command execution
            std::string output = cmd + "\n";
            std::string prompt;
            if (cmd.find("interface") != std::string::npos) {
                prompt = "(interface)# ";
            }
            else if (cmd.find("router rip") != std::string::npos) {
                prompt = "(router-rip)# ";
            }
            else {
                prompt = "(config)# ";
            }
            EXPECT_CALL(*mockConsole, print(expectedOutput))
                .Times(1);
            EXPECT_CALL(*mockConsole, print(prompt))
                .Times(1);
        }

        bool result = handleInput(cmd);
        if (cmd.find("invalidcmd") != std::string::npos) {
            EXPECT_FALSE(result);
        }
        else {
            EXPECT_TRUE(result);
        }
    }

    // Mock the readFile to return the saved configuration (excluding 'save config' and invalid command)
    json savedConfig;
    savedConfig["commands"] = {"hostname MixedRouter", "interface GigabitEthernet0/1", "ip address 10.0.0.1 255.255.255.0",
                                "description Main Interface", "router rip", "network 10.0.0.0", "exit"};

    EXPECT_CALL(*mockFileSystem, readFile("../configs.json", _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string& path, std::string& content) -> bool {
            content = savedConfig.dump();
            return true;
        }));

    // Expectation: Terminal executes the recovered commands
    for (const auto& cmd : savedConfig["commands"]) {
        std::string output = cmd + "\n";
        std::string prompt;
        if (cmd.find("interface") != std::string::npos) {
            prompt = "(interface)# ";
        }
        else if (cmd.find("router rip") != std::string::npos) {
            prompt = "(router-rip)# ";
        }
        else {
            prompt = "(config)# ";
        }
        EXPECT_CALL(*mockConsole, print(output))
            .Times(1);
        EXPECT_CALL(*mockConsole, print(prompt))
            .Times(1);
        recoveredCommands.push_back(cmd);
    }

    // Act: Recover state
    bool recoveryResult = terminal->recoverState();

    // Assert
    EXPECT_TRUE(recoveryResult);
    EXPECT_EQ(terminal->getHostname(), "MixedRouter");
    EXPECT_TRUE(terminal->isInterfaceConfigured("GigabitEthernet0/1"));
    EXPECT_TRUE(terminal->isIpAddressAssigned("GigabitEthernet0/1", "10.0.0.1", "255.255.255.0"));
    EXPECT_TRUE(terminal->isInterfaceDescriptionSet("GigabitEthernet0/1", "Main Interface"));
    EXPECT_TRUE(terminal->isRoutingProtocolConfigured("RIP", 1));
    EXPECT_TRUE(terminal->isRipNetworkConfigured("10.0.0.0"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 21. Additional Helper and Utility Tests

// Test 21.1: Tokenizing a string with spaces and special characters
TEST_F(TerminalTest, Utility_TokenizeString_ShouldSplitCorrectly) {
    // Arrange
    std::string input = "interface GigabitEthernet0/1 ip address 10.0.0.1 255.255.255.0";
    std::vector<std::string> expectedTokens = {"interface", "GigabitEthernet0/1", "ip", "address", "10.0.0.1", "255.255.255.0"};

    // Act
    std::vector<std::string> tokens = terminal->tokenize(input, ' ');

    // Assert
    EXPECT_EQ(tokens, expectedTokens);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.2: Trimming leading and trailing spaces
TEST_F(TerminalTest, Utility_TrimString_ShouldRemoveLeadingAndTrailingSpaces) {
    // Arrange
    std::string input = "   hostname RouterTrim   ";
    std::string expected = "hostname RouterTrim";

    // Act
    std::string trimmed = terminal->trimString(input);

    // Assert
    EXPECT_EQ(trimmed, expected);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.3: Checking if a string is numeric
TEST_F(TerminalTest, Utility_IsNumeric_ShouldIdentifyNumericStrings) {
    // Arrange & Act & Assert
    EXPECT_TRUE(terminal->isNumeric("12345"));
    EXPECT_TRUE(terminal->isNumeric("-6789"));
    EXPECT_FALSE(terminal->isNumeric("12a45"));
    EXPECT_FALSE(terminal->isNumeric("abcde"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.4: Checking if a string is a valid MAC address
TEST_F(TerminalTest, Utility_IsMACAddress_ShouldValidateCorrectly) {
    // Arrange & Act & Assert
    EXPECT_TRUE(terminal->isMACAddress("00:1A:2B:3C:4D:5E"));
    EXPECT_TRUE(terminal->isMACAddress("00-1A-2B-3C-4D-5E"));
    EXPECT_FALSE(terminal->isMACAddress("001A.2B3C.4D5E"));
    EXPECT_FALSE(terminal->isMACAddress("00:1A:2B:3C:4D"));
    EXPECT_FALSE(terminal->isMACAddress("GG:HH:II:JJ:KK:LL"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.5: Checking if a string is a valid IPv6 address
TEST_F(TerminalTest, Utility_IsIPv6Address_ShouldValidateCorrectly) {
    // Arrange & Act & Assert
    EXPECT_TRUE(terminal->isIPv6Address("2001:0db8:85a3:0000:0000:8a2e:0370:7334"));
    EXPECT_TRUE(terminal->isIPv6Address("2001:db8::1"));
    EXPECT_FALSE(terminal->isIPv6Address("2001:0db8:85a3::8a2e:0370:7334:"));
    EXPECT_FALSE(terminal->isIPv6Address("2001:0db8:85a3:0000:0000:8a2e:0370"));
    EXPECT_FALSE(terminal->isIPv6Address("2001:0db8:85a3:0000:0000:8a2e:0370:7334:1234"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.6: Matching input patterns for a valid volatile command
TEST_F(TerminalTest, PatternMatching_ValidVolatileCommand_ShouldMatchPattern) {
    // Arrange
    std::string userInput = "do ping 192.168.1.1";
    std::string expectedPattern = "ping [IP_ADDRESS]";

    // Act
    bool matches = terminal->matchInputPattern(userInput, expectedPattern);

    // Assert
    EXPECT_TRUE(matches);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.7: Matching input patterns for an invalid volatile command
TEST_F(TerminalTest, PatternMatching_InvalidVolatileCommand_ShouldNotMatchPattern) {
    // Arrange
    std::string userInput = "do traceroute";
    std::string expectedPattern = "traceroute [IP_ADDRESS]";

    // Act
    bool matches = terminal->matchInputPattern(userInput, expectedPattern);

    // Assert
    EXPECT_FALSE(matches);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.8: Expanding IPv6 address correctly
TEST_F(TerminalTest, IPv6Expanding_ValidCompressedAddress_ShouldExpandSuccessfully) {
    // Arrange
    std::string compressedIPv6 = "2001:db8::1";
    std::string expectedExpanded = "2001:0db8:0000:0000:0000:0000:0000:0001";

    // Act
    std::string expanded = terminal->expandIPv6Address(compressedIPv6);

    // Assert
    EXPECT_EQ(expanded, expectedExpanded);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.9: Expanding IPv6 address with multiple '::' should handle error
TEST_F(TerminalTest, IPv6Expanding_InvalidCompressedAddress_ShouldHandleError) {
    // Arrange
    std::string compressedIPv6 = "2001::db8::1";
    std::string expectedError = "Error: Invalid IPv6 address format.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the invalid IPv6 address
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    std::string expanded = terminal->expandIPv6Address(compressedIPv6);

    // Assert
    EXPECT_EQ(expanded, "");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 22. Insert Mode Handling Tests

// Test 22.1: Toggling insert mode on and inserting a character
TEST_F(TerminalTest, InsertMode_ToggleOnAndInsertCharacter_ShouldInsertSuccessfully) {
    // Arrange
    std::string toggleInsertCmd = "insert on";
    std::string toggleInsertOutput = "Insert mode enabled.\n";
    std::string expectedPrompt = "(config)# ";

    std::string insertCommand = "insert e";
    std::string expectedInsertOutput = "e";
    std::string expectedClearTail = "\033[K";

    // Expectation: Toggle insert mode
    EXPECT_CALL(*mockConsole, print(toggleInsertOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act: Toggle insert mode on
    bool toggleResult = handleInput(toggleInsertCmd);
    EXPECT_TRUE(toggleResult);

    // Expectation: Insert character 'e'
    EXPECT_CALL(*mockConsole, print(expectedInsertOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedClearTail))
        .Times(1);

    // Act: Insert character
    terminal->insertCharacter('e', 0); // Assuming method signature
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 22.2: Toggling insert mode off and overwriting a character
TEST_F(TerminalTest, InsertMode_ToggleOffAndOverwriteCharacter_ShouldOverwriteSuccessfully) {
    // Arrange
    std::string toggleInsertCmd = "insert off";
    std::string toggleInsertOutput = "Insert mode disabled.\n";
    std::string expectedPrompt = "(config)# ";

    std::string overwriteCommand = "overwrite e";
    std::string expectedOverwriteOutput = "\be";
    std::string expectedClearTail = "\033[K";

    // Expectation: Toggle insert mode off
    EXPECT_CALL(*mockConsole, print(toggleInsertOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act: Toggle insert mode off
    bool toggleResult = handleInput(toggleInsertCmd);
    EXPECT_TRUE(toggleResult);

    // Expectation: Overwrite character 'e'
    EXPECT_CALL(*mockConsole, print(expectedOverwriteOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedClearTail))
        .Times(1);

    // Act: Overwrite character
    terminal->overwriteCharacter('e', 0); // Assuming method signature
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 23. Global Commands Validation Tests

// Test 23.1: Executing global command 'do show version'
TEST_F(TerminalTest, GlobalCommand_DoShowVersion_ShouldExecuteSuccessfully) {
    // Arrange
    std::string command = "do show version";
    std::string expectedOutput = "Router OS Version 1.0.0\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal executes the "do" command and prints output
    EXPECT_CALL(*mockConsole, print(expectedOutput + "\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 23.2: Executing global command 'do' with invalid subcommand
TEST_F(TerminalTest, GlobalCommand_DoWithInvalidSubcommand_ShouldRejectCommand) {
    // Arrange
    std::string command = "do invalidsubcmd";
    std::string expectedError = "Error: Unknown subcommand 'invalidsubcmd' for 'do'.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the "do" command and prints error
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 24. Cleanup and Teardown Tests

// Test 24.1: Resetting the terminal state should clear all configurations
TEST_F(TerminalTest, Reset_TerminalState_ShouldClearAllConfigurations) {
    // Arrange
    // First, execute some commands
    std::vector<std::string> commands = {
        "hostname ResetRouter",
        "interface GigabitEthernet0/1",
        "ip address 10.0.0.1 255.255.255.0"
    };
    std::vector<std::string> expectedOutputs = {
        "hostname ResetRouter\n",
        "interface GigabitEthernet0/1\n",
        "ip address 10.0.0.1 255.255.255.0\n"
    };
    std::string expectedPrompt = "(config-if)# ";

    for (size_t i = 0; i < commands.size(); ++i) {
        const std::string& cmd = commands[i];
        const std::string& output = expectedOutputs[i];
        std::string prompt;
        if (cmd.find("interface") != std::string::npos) {
            prompt = "(interface)# ";
        }
        else {
            prompt = "(config)# ";
        }

        EXPECT_CALL(*mockConsole, print(output))
            .Times(1);
        EXPECT_CALL(*mockConsole, print(prompt))
            .Times(1);

        bool result = handleInput(cmd);
        EXPECT_TRUE(result);
    }

    // Act: Reset the terminal
    terminal->reset();

    // Assert: Verify that configurations are cleared
    EXPECT_EQ(terminal->getHostname(), "");
    EXPECT_FALSE(terminal->isInterfaceConfigured("GigabitEthernet0/1"));
    EXPECT_FALSE(terminal->isIpAddressAssigned("GigabitEthernet0/1", "10.0.0.1", "255.255.255.0"));
    EXPECT_EQ(terminal->getCurrentMode(), "(config)# ");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 24.2: Recovering state after reset should not restore configurations
TEST_F(TerminalTest, StateRecovery_AfterReset_ShouldNotRestoreConfigurations) {
    // Arrange
    // Assuming terminal has been reset in previous test
    // Mock the readFile to return an empty configuration
    json savedConfig;
    savedConfig["commands"] = {};

    EXPECT_CALL(*mockFileSystem, readFile("../configs.json", _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string& path, std::string& content) -> bool {
            content = savedConfig.dump();
            return true;
        }));

    // Act: Recover state
    bool recoveryResult = terminal->recoverState();

    // Assert
    EXPECT_TRUE(recoveryResult);
    EXPECT_EQ(terminal->getHostname(), "");
    EXPECT_EQ(terminal->getCurrentMode(), "(config)# ");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 25. Comprehensive Edge Case Tests

// Test 25.1: Processing a command with embedded quotes
TEST_F(TerminalTest, EdgeCase_CommandWithEmbeddedQuotes_ShouldHandleGracefully) {
    // Arrange
    std::string command = "hostname \"Router with Quotes\"";
    std::string expectedOutput = "hostname Router with Quotes\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal processes the command and strips quotes
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(terminal->getHostname(), "Router with Quotes");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 25.2: Processing a command with unicode characters
TEST_F(TerminalTest, EdgeCase_CommandWithUnicodeCharacters_ShouldRejectCommand) {
    // Arrange
    std::string command = "hostname RôûtêrÜñîçødê";
    std::string expectedError = "Error: Invalid characters in hostname.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to unicode characters
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(terminal->getHostname(), "");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 25.3: Processing multiple consecutive invalid commands
TEST_F(TerminalTest, EdgeCase_MultipleConsecutiveInvalidCommands_ShouldHandleEachAppropriately) {
    // Arrange
    std::vector<std::string> commands = {"invalid1", "invalid2", "invalid3"};
    std::vector<std::string> expectedErrors = {
        "Error: Unknown command 'invalid1'.\n",
        "Error: Unknown command 'invalid2'.\n",
        "Error: Unknown command 'invalid3'.\n"
    };
    std::string expectedPrompt = "(config)# ";

    for (size_t i = 0; i < commands.size(); ++i) {
        EXPECT_CALL(*mockConsole, print(commands[i] + "\n"))
            .Times(1);
        EXPECT_CALL(*mockConsole, print(expectedErrors[i]))
            .Times(1);
        EXPECT_CALL(*mockConsole, print(expectedPrompt))
            .Times(1);

        // Act
        bool result = handleInput(commands[i]);

        // Assert
        EXPECT_FALSE(result);
    }
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 25.4: Processing a command with numeric parameters beyond valid range
TEST_F(TerminalTest, EdgeCase_CommandWithNumericParametersOutOfRange_ShouldRejectCommand) {
    // Arrange
    std::string command = "interface GigabitEthernet0/999";
    std::string expectedError = "Error: Interface 'GigabitEthernet0/999' is out of valid range.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to out-of-range interface number
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 25.5: Processing a command with special shell characters
TEST_F(TerminalTest, EdgeCase_CommandWithShellCharacters_ShouldRejectCommand) {
    // Arrange
    std::string command = "hostname Router; rm -rf /";
    std::string expectedError = "Error: Invalid characters in hostname.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal rejects the command due to special characters
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 26. Help and Auto-Completion Edge Cases

// Test 26.1: Auto-completing a partial command with no matches
TEST_F(TerminalTest, AutoComplete_NoMatchingCommands_ShouldHandleGracefully) {
    // Arrange
    std::string partialInput = "xyz";
    std::string expectedError = "Error: No commands match 'xyz'.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = terminal->autoComplete(partialInput);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(partialInput, "xyz"); // Input remains unchanged
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 26.2: Auto-completing a partial command that is a prefix to multiple commands
TEST_F(TerminalTest, AutoComplete_PrefixToMultipleCommands_ShouldListSuggestions) {
    // Arrange
    std::string partialInput = "int";
    std::vector<std::string> suggestions = {"interface", "invalidcmd"};
    std::string expectedOutput;
    for (const auto& cmd : suggestions) {
        expectedOutput += cmd + "\n";
    }
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal lists available suggestions
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = terminal->autoComplete(partialInput);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(partialInput, "int"); // Input remains unchanged
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 26.3: Auto-completing with trailing space should not trigger auto-complete
TEST_F(TerminalTest, AutoComplete_TrailingSpace_ShouldNotTriggerAutoComplete) {
    // Arrange
    std::string partialInput = "hostname ";
    std::string expectedOutput = ""; // No auto-completion expected

    // Expectation: Terminal does not attempt to auto-complete
    EXPECT_CALL(*mockConsole, print(_))
        .Times(0);

    // Act
    bool result = terminal->autoComplete(partialInput);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(partialInput, "hostname ");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 27. Command History Navigation Tests

// Note: Assuming the Terminal class has methods to navigate command history, such as historyUp() and historyDown()

// Test 27.1: Navigating up through command history retrieves previous command
TEST_F(TerminalTest, CommandHistory_NavigateUp_ShouldRetrievePreviousCommand) {
    // Arrange
    std::string firstCommand = "hostname HistoryRouter1";
    std::string secondCommand = "interface GigabitEthernet0/1";

    std::string expectedOutput1 = "hostname HistoryRouter1\n";
    std::string expectedPrompt1 = "(config)# ";

    std::string expectedOutput2 = "interface GigabitEthernet0/1\n";
    std::string expectedPrompt2 = "(interface)# ";

    // Execute first command
    EXPECT_CALL(*mockConsole, print(expectedOutput1))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt1))
        .Times(1);
    bool result1 = handleInput(firstCommand);
    EXPECT_TRUE(result1);

    // Execute second command
    EXPECT_CALL(*mockConsole, print(expectedOutput2))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt2))
        .Times(1);
    bool result2 = handleInput(secondCommand);
    EXPECT_TRUE(result2);

    // Navigate up to retrieve second command
    EXPECT_CALL(*mockConsole, print(expectedOutput2))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt2))
        .Times(1);
    std::string retrievedCommand = terminal->historyUp();
    EXPECT_EQ(retrievedCommand, secondCommand);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 27.2: Navigating down through command history retrieves next command
TEST_F(TerminalTest, CommandHistory_NavigateDown_ShouldRetrieveNextCommand) {
    // Arrange
    std::string firstCommand = "hostname HistoryRouter1";
    std::string secondCommand = "interface GigabitEthernet0/1";

    std::string expectedOutput1 = "hostname HistoryRouter1\n";
    std::string expectedPrompt1 = "(config)# ";

    std::string expectedOutput2 = "interface GigabitEthernet0/1\n";
    std::string expectedPrompt2 = "(interface)# ";

    // Execute first command
    EXPECT_CALL(*mockConsole, print(expectedOutput1))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt1))
        .Times(1);
    bool result1 = handleInput(firstCommand);
    EXPECT_TRUE(result1);

    // Execute second command
    EXPECT_CALL(*mockConsole, print(expectedOutput2))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt2))
        .Times(1);
    bool result2 = handleInput(secondCommand);
    EXPECT_TRUE(result2);

    // Navigate up to retrieve second command
    EXPECT_CALL(*mockConsole, print(expectedOutput2))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt2))
        .Times(1);
    std::string retrievedCommand = terminal->historyUp();
    EXPECT_EQ(retrievedCommand, secondCommand);

    // Navigate down to retrieve first command
    EXPECT_CALL(*mockConsole, print(expectedOutput1))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt1))
        .Times(1);
    retrievedCommand = terminal->historyDown();
    EXPECT_EQ(retrievedCommand, firstCommand);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 27.3: Navigating beyond history boundaries should handle gracefully
TEST_F(TerminalTest, CommandHistory_NavigateBeyondBoundaries_ShouldHandleGracefully) {
    // Arrange
    std::string command = "hostname BoundaryRouter";
    std::string expectedOutput = "hostname BoundaryRouter\n";
    std::string expectedPrompt = "(config)# ";

    // Execute command
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);
    bool result = handleInput(command);
    EXPECT_TRUE(result);

    // Navigate up to retrieve the command
    EXPECT_CALL(*mockConsole, print(expectedOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);
    std::string retrievedCommand = terminal->historyUp();
    EXPECT_EQ(retrievedCommand, command);

    // Navigate up beyond history
    EXPECT_CALL(*mockConsole, print("No more commands in history.\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);
    retrievedCommand = terminal->historyUp();
    EXPECT_EQ(retrievedCommand, ""); // Assuming empty string when no history
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 28. Save and Load Configuration Tests

// Test 28.1: Saving configuration should write commands to file
TEST_F(TerminalTest, SaveConfiguration_ShouldWriteCommandsToFile) {
    // Arrange
    std::string command1 = "hostname SaveRouter";
    std::string command2 = "interface GigabitEthernet0/1";

    std::string expectedOutput1 = "hostname SaveRouter\n";
    std::string expectedPrompt1 = "(config)# ";

    std::string expectedOutput2 = "interface GigabitEthernet0/1\n";
    std::string expectedPrompt2 = "(interface)# ";

    // Execute commands
    EXPECT_CALL(*mockConsole, print(expectedOutput1))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt1))
        .Times(1);
    bool result1 = handleInput(command1);
    EXPECT_TRUE(result1);

    EXPECT_CALL(*mockConsole, print(expectedOutput2))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt2))
        .Times(1);
    bool result2 = handleInput(command2);
    EXPECT_TRUE(result2);

    // Expectation: Save configuration
    std::string saveCommand = "save config";
    std::string saveOutput = "Configuration saved successfully.\n";

    EXPECT_CALL(*mockConsole, print(saveOutput))
        .Times(1);
    EXPECT_CALL(*mockFileSystem, writeFile("../configs.json", _))
        .Times(1)
        .WillOnce(Return(true));

    // Act
    bool saveResult = terminal->saveConfiguration();
    EXPECT_TRUE(saveResult);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 28.2: Loading configuration should restore commands from file
TEST_F(TerminalTest, LoadConfiguration_ShouldRestoreCommandsFromFile) {
    // Arrange
    // Mock the readFile to return saved commands
    json savedConfig;
    savedConfig["commands"] = {"hostname LoadRouter", "interface GigabitEthernet0/2"};

    EXPECT_CALL(*mockFileSystem, readFile("../configs.json", _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string& path, std::string& content) -> bool {
            content = savedConfig.dump();
            return true;
        }));

    // Expectation: Terminal executes the recovered commands
    for (const auto& cmd : savedConfig["commands"]) {
        std::string output = cmd + "\n";
        std::string prompt = (cmd.find("interface") != std::string::npos) ? "(interface)# " : "(config)# ";
        EXPECT_CALL(*mockConsole, print(output))
            .Times(1);
        EXPECT_CALL(*mockConsole, print(prompt))
            .Times(1);
    }

    // Act
    bool loadResult = terminal->loadConfiguration();

    // Assert
    EXPECT_TRUE(loadResult);
    EXPECT_EQ(terminal->getHostname(), "LoadRouter");
    EXPECT_TRUE(terminal->isInterfaceConfigured("GigabitEthernet0/2"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 28.3: Loading configuration from a corrupted JSON should handle error
TEST_F(TerminalTest, LoadConfiguration_CorruptedJSON_ShouldHandleError) {
    // Arrange
    std::string corruptedContent = "{invalid_json}";
    std::string expectedError = "Error: Failed to parse configuration file.\n";
    std::string expectedPrompt = "(config)# ";

    EXPECT_CALL(*mockFileSystem, readFile("../configs.json", _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string& path, std::string& content) -> bool {
            content = corruptedContent;
            return true;
        }));

    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool loadResult = terminal->loadConfiguration();

    // Assert
    EXPECT_FALSE(loadResult);
    EXPECT_EQ(terminal->getHostname(), ""); // No hostname should be set
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 29. Comprehensive Mode Transition Tests

// Test 29.1: Switching from global mode to interface mode and back
TEST_F(TerminalTest, ModeTransition_GlobalToInterfaceAndBack_ShouldHandleCorrectly) {
    // Arrange
    std::string interfaceCmd = "interface GigabitEthernet0/1";
    std::string interfaceOutput = "interface GigabitEthernet0/1\n";
    std::string interfacePrompt = "(interface)# ";

    std::string exitCmd = "exit";
    std::string exitOutput = "exit\n";
    std::string globalPrompt = "(config)# ";

    // Expectation: Enter interface mode
    EXPECT_CALL(*mockConsole, print(interfaceOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(interfacePrompt))
        .Times(1);

    // Act: Enter interface mode
    bool result1 = handleInput(interfaceCmd);
    EXPECT_TRUE(result1);

    // Expectation: Exit interface mode
    EXPECT_CALL(*mockConsole, print(exitOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(globalPrompt))
        .Times(1);

    // Act: Exit interface mode
    bool result2 = handleInput(exitCmd);
    EXPECT_TRUE(result2);

    // Assert
    EXPECT_EQ(terminal->getCurrentMode(), "(config)# ");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 29.2: Switching to routing protocol sub-mode and executing commands
TEST_F(TerminalTest, ModeTransition_RoutingProtocolSubMode_ShouldHandleCommands) {
    // Arrange
    std::string routingCmd = "router eigrp 100";
    std::string routingOutput = "router eigrp 100\n";
    std::string routingPrompt = "(router-eigrp)# ";

    std::string networkCmd = "network 10.0.0.0 0.0.0.255";
    std::string networkOutput = "network 10.0.0.0 0.0.0.255\n";
    std::string routingPromptAfter = "(router-eigrp)# ";

    // Expectation: Enter routing protocol sub-mode
    EXPECT_CALL(*mockConsole, print(routingOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(routingPrompt))
        .Times(1);

    // Act: Enter routing protocol sub-mode
    bool result1 = handleInput(routingCmd);
    EXPECT_TRUE(result1);

    // Expectation: Execute network command within sub-mode
    EXPECT_CALL(*mockConsole, print(networkOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(routingPromptAfter))
        .Times(1);

    // Act: Execute network command
    bool result2 = handleInput(networkCmd);
    EXPECT_TRUE(result2);

    // Assert
    EXPECT_TRUE(terminal->isRoutingProtocolConfigured("EIGRP", 100));
    EXPECT_TRUE(terminal->isEigrpNetworkConfigured("10.0.0.0", "0.0.0.255"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 29.3: Switching between multiple sub-modes consecutively
TEST_F(TerminalTest, ModeTransition_MultipleSubModes_ShouldHandleCorrectly) {
    // Arrange
    // Enter first sub-mode
    std::string subMode1Cmd = "router rip";
    std::string subMode1Output = "router rip\n";
    std::string subMode1Prompt = "(router-rip)# ";

    // Enter second sub-mode from first sub-mode
    std::string subMode2Cmd = "network 192.168.1.0";
    std::string subMode2Output = "network 192.168.1.0\n";
    std::string subMode2Prompt = "(router-rip)# ";

    // Exit to global mode
    std::string exitCmd = "exit";
    std::string exitOutput = "exit\n";
    std::string globalPrompt = "(config)# ";

    // Expectation: Enter first sub-mode
    EXPECT_CALL(*mockConsole, print(subMode1Output))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(subMode1Prompt))
        .Times(1);

    // Act: Enter first sub-mode
    bool result1 = handleInput(subMode1Cmd);
    EXPECT_TRUE(result1);

    // Expectation: Execute network command within first sub-mode
    EXPECT_CALL(*mockConsole, print(subMode2Output))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(subMode2Prompt))
        .Times(1);

    // Act: Execute network command within first sub-mode
    bool result2 = handleInput(subMode2Cmd);
    EXPECT_TRUE(result2);

    // Expectation: Exit to global mode
    EXPECT_CALL(*mockConsole, print(exitOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(globalPrompt))
        .Times(1);

    // Act: Exit to global mode
    bool result3 = handleInput(exitCmd);
    EXPECT_TRUE(result3);

    // Assert
    EXPECT_EQ(terminal->getCurrentMode(), "(config)# ");
    EXPECT_TRUE(terminal->isRoutingProtocolConfigured("RIP", 1));
    EXPECT_TRUE(terminal->isRipNetworkConfigured("192.168.1.0"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// 30. Additional Comprehensive Tests

// Test 30.1: Processing a command with both "do" and normalization
TEST_F(TerminalTest, Comprehensive_DoAndNormalization_ShouldProcessSuccessfully) {
    // Arrange
    std::string command = "do sh run";
    std::string normalizedCommand = "do show running-config";
    std::string expectedOutput = "Running Configuration:\n...";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(expectedOutput + "\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 30.2: Processing a "do" command with normalization and invalid subcommand
TEST_F(TerminalTest, Comprehensive_DoAndNormalization_InvalidSubcommand_ShouldRejectCommand) {
    // Arrange
    std::string command = "do sh invalidsubcmd";
    std::string normalizedCommand = "do show invalidsubcmd";
    std::string expectedError = "Error: 'invalidsubcmd' is not a valid subcommand for 'show'.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal normalizes and rejects the command
    EXPECT_CALL(*mockConsole, print(normalizedCommand + "\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 30.3: Processing a "do" command with excessive arguments after normalization
TEST_F(TerminalTest, Comprehensive_DoAndNormalization_ExcessiveArguments_ShouldRejectCommand) {
    // Arrange
    std::string command = "do sh run extraArg";
    std::string normalizedCommand = "do show running-config extraArg";
    std::string expectedError = "Error: 'show running-config' command takes no arguments.\n";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal normalizes and rejects the command
    EXPECT_CALL(*mockConsole, print(normalizedCommand + "\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedError))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 30.4: Processing a "do" command that triggers help request
TEST_F(TerminalTest, Comprehensive_DoCommand_TriggersHelp_ShouldDisplayHelp) {
    // Arrange
    std::string command = "do show ?";
    std::vector<std::string> availableSubCommands = {"running-config", "startup-config", "interfaces"};
    std::string expectedHelpOutput;
    for (const auto& cmd : availableSubCommands) {
        expectedHelpOutput += cmd + "\n";
    }
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal executes "do show", detects '?', and displays help
    EXPECT_CALL(*mockConsole, print("do show\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedHelpOutput))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 30.5: Processing a "do" command with IPv6 address
TEST_F(TerminalTest, Comprehensive_DoCommand_WithIPv6Address_ShouldProcessSuccessfully) {
    // Arrange
    std::string command = "do ping 2001:db8::1";
    std::string expectedOutput = "Pinging 2001:db8::1...\nSuccess!";
    std::string expectedPrompt = "(config)# ";

    // Expectation: Terminal processes the command and prints output
    EXPECT_CALL(*mockConsole, print(expectedOutput + "\n"))
        .Times(1);
    EXPECT_CALL(*mockConsole, print(expectedPrompt))
        .Times(1);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}
*/
