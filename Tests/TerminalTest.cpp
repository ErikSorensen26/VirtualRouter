#include <gtest/gtest.h>
#include <Terminal.h>
#include <MockConsole.hpp>
#include <MockFileSystem.hpp>

using json = nlohmann::json;

// Declare the test class as a friend to access private members
class TerminalTest : public ::testing::Test
{
public:
    // Mock objects
    std::shared_ptr<testing::NiceMock<ReducedMockConsole>> mockConsole;
    static std::shared_ptr<testing::NiceMock<MockFileSystem>> mockFileSystem;
protected:

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
        mockConsole->resetCapturedOutput();
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

    // Other functions
    bool batchProcessAndRecover(const std::vector<std::string>& commands, const std::vector<std::string>& expectedOutputs, std::vector<std::string>& recoveredCommands);
    
    static void TearDownTestSuite() {}

public:
    // Helper functions
    bool executeCommand(std::string& command) {return handleInput(command);}
    bool handleInput(const std::string& command) {return terminal->handleInput(command);}
    void changeMode(std::string& newMode) {terminal->changeMode(newMode);}
    void configureRoutingMode(std::string& newMode) {terminal->configureRoutingMode(newMode);}
    void configureInterfaceMode(std::string& interface) {terminal->configureInterfaceMode(interface);}
    std::string getHostname() {return Global::getInstance().getHostname();}
    std::string getCurrentMode() {return terminal->currentMode;}
    std::string getCurrentSubMode() {return terminal->currentSubMode;}
    std::string getNextLine() {return terminal->nextLine;}
    const json& getCommandTree() const {return terminal->commandTree;}
    const json& getWorkingDirectory() const {return terminal->workingDirectory;}
    std::string normalizeCommand(std::string& command) {return terminal->normalizeCommand(command);}
    void initialize() {terminal->initializeProcessingState();}
    std::string expandIPv6Address(std::string ip) {return terminal->expandIPv6Address(ip);}
    bool save() {return terminal->saveConfig();}
    std::vector<std::string> recoverConfigs(nlohmann::ordered_json& json) {return terminal->recoverConfigs(&json);}
    nlohmann::ordered_json getRoot() {return terminal->root;}
    bool isNumeric(const std::string num) {return terminal->isNumeric(num);}
    bool isMACAddress(const std::string mac) {return terminal->isMACAddress(mac);}
    bool isIPv6Address(const std::string ip) {return terminal->isIPv6Address(ip);}
};

// Static Initialization
std::shared_ptr<IFileSystem> TerminalTest::realFileSystem = nullptr;
std::shared_ptr<testing::NiceMock<MockFileSystem>> TerminalTest::mockFileSystem = nullptr;
std::string TerminalTest::commandTreeString;
std::string TerminalTest::configSchemaString;
std::string TerminalTest::configFileString;
#pragma region ModeChange

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

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#\n");
}

// Test 3.2: Handling input with only spaces should reject command
TEST_F(TerminalTest, InputHandling_SpacesOnly_ShouldRejectCommand) 
{
    // Arrange
    std::string command = "   \n";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(5);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#   \n");
}

// Test 3.3: Handling valid input with leading and trailing spaces
TEST_F(TerminalTest, InputHandling_ValidInputWithSpaces_ShouldProcessCommand) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string rawCommand = "  hostname Router1  \n";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(22);

    // Act
    bool result = handleInput(rawCommand);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(getHostname(), "Router1");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#  hostname Router1  \n");
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
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(24);

    // Act
    bool result = handleInput(doCommand);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#do show running-config\n");
}

// Test Executing an invalid "do" command
TEST_F(TerminalTest, DoCommand_InvalidCommand_ShouldRejectCommand) {
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string doCommand = "do invalidcmd\n";

    // Expectation: Terminal rejects the "do" command and prints error
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(16);

    // Act
    bool result = handleInput(doCommand);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#do invalidcmd\n               ^\n% Invlid input detected at '^' marker.\n\n");
}

// Test Executing a "do" command with missing parameters
TEST_F(TerminalTest, DoCommand_MissingParameters_ShouldRejectCommand) {
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string doCommand = "do ping\n";

    // Expectation: Terminal rejects the "do" command due to missing parameters
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(10);

    // Act
    bool result = handleInput(doCommand);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#do ping\nIncomplete Command\n");
}

// Test Executing a "do" command from sub-mode
TEST_F(TerminalTest, DoCommand_FromSubMode_ShouldExecuteCommandWithinSubMode) 
{
    // Arrange
    // First, enter sub-mode
    changeMode(mode.globalConfiguration);
    std::string enterSubModeCmd = "interface GigabitEthernet 1\n";
    std::string doCommand = "do show interface GigabitEthernet 1";

    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(66);

    // Act: Enter sub-mode
    bool result1 = handleInput(enterSubModeCmd);
    EXPECT_TRUE(result1);

    // Now, execute "do" command within sub-mode

    // Act: Execute "do" command within sub-mode
    bool result2 = handleInput(doCommand);
    EXPECT_TRUE(result2);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#interface GigabitEthernet 1\nrouter(config-if)#do show interface GigabitEthernet 1\n");
}

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
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(16);

    // Act
    bool result = handleInput(helpCommand);

    // Assert
    EXPECT_TRUE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router>?\n  <1-99>          Session number to resume\n  connect         Open a terminal connection\n  disable         Turn off privileged commands\n  disconnect      Disconnect an existing network connection\n  enable          Turn on privileged commands\n  logout          Exit from the EXEC\n  ping            Send echo messages\n  resume          Resume an active network connection\n  show            Show running system information\n  ssh             Open a secure shell client connection\n  telnet          Open a telnet connection\n  terminal        Set terminal line parameters\n  traceroute      Trace route to destination\n");
}

// Test Auto-completing a unique partial command using Tab
TEST_F(TerminalTest, AutoComplete_UniquePartialCommand_ShouldCompleteCommand) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string partialInput = "host\t";
    std::string finishInput = " Router1\n";

    // Expectation: Terminal auto-completes the command
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(17);

    // Act
    bool result = handleInput(partialInput);
    bool result2 = handleInput(finishInput);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_TRUE(result2);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#host\nrouter(config)#hostname  Router1\n");
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
    EXPECT_TRUE(result);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#a\n");
    EXPECT_EQ(getNextLine(), "a ");
}

// Test Auto-completing an exact command should do nothing
TEST_F(TerminalTest, AutoComplete_ExactCommand_ShouldNotChangeInput) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string exactCommand = "exit\t";

    // Expectation: Terminal does not attempt to auto-complete
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(6);

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

    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(57);

    // Act: Enter sub-mode
    bool result1 = handleInput(enterSubModeCmd);
    EXPECT_TRUE(result1);

    // Act: Invoke help in sub-mode
    bool result2 = handleInput(helpCommand);
    EXPECT_TRUE(result2);
    std::string help = "router(config)#interface GigabitEthernet 1\nrouter(config-if)#?\n  arp                    Set arp type (arpa, probe, snap) or timeout\n  bandwidth              Set bandwidth informational parameter\n  cdp                    CDP interface subcommands\n  channel-group          Add this interface to an Etherchannel group\n  crypto                 Encryption/Decryption commands\n  custom-queue-list      Assign a custom queue list to an interface\n  delay                  Specify interface throughput delay\n  description            Interface specific description\n  duplex                 Configure duplex operation.\n  fair-queue             Enable Fair Queuing on an Interface\n  hold-queue             Set hold queue depth\n  ip                     Interface Internet Protocol config \n  ipv6                   IPv6 interface subcommands\n  lldp                   LLDP interface subcommands\n  mac-address            Manually set interface MAC address\n  mtu                    Set the interface Maximum Transmission Unit (MTU)\n  no                     Negate a command or set its defaults\n  pppoe                  pppoe interface subcommands\n  pppoe-client           pppoe client\n  priority-group         Assign a priority group to an interface\n  service-policy         Configure QoS Service Policy\n  shutdown               Shutdown the selected interface\n  speed                  Configure speed operation.\n  standby                HSRP interface configuration commands\n  tx-ring-limit          Configure PA level transmit ring limit\n";
}

#pragma endregion
#pragma region CommandProcessing

// Test Processing a valid global command
TEST_F(TerminalTest, CommandProcessing_ValidGlobalCommand_ShouldProcessSuccessfully) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string command = "hostname Router1\n";

    // Expectation: Terminal prints the command and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(18);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(Global::getInstance().getHostname(), "Router1");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#hostname Router1\n");
}

// Test Processing an invalid global command
TEST_F(TerminalTest, CommandProcessing_InvalidGlobalCommand_ShouldRejectCommand) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string command = "invalidcmd\n";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(13);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#invalidcmd\n               ^\n% Invlid input detected at '^' marker.\n\n");
}

// Test Processing a command with missing required arguments
TEST_F(TerminalTest, CommandProcessing_MissingArguments_ShouldRejectCommand) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string command = "hostname\n"; // Missing hostname value

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(11);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(Global::getInstance().getHostname(), "router"); // Hostname should remain default
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#hostname\nIncomplete Command\n");
}

// Test Processing a command with excessive arguments
TEST_F(TerminalTest, CommandProcessing_ExcessiveArguments_ShouldRejectCommand) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string command = "hostname Router1 ExtraArg\n";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(28);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(Global::getInstance().getHostname(), "router"); // Hostname should remain default
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#hostname Router1 ExtraArg\n                                ^\n% Invlid input detected at '^' marker.\n\n");
}

// Test Processing a volatile command with pattern matching
TEST_F(TerminalTest, CommandProcessing_VolatileCommand_ShouldValidatePatterns) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string command = "do ping 192.168.1.1\n";

    // Expectation: Terminal processes the command and prints output
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(21);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#do ping 192.168.1.1\n");
}

// Test Processing a volatile command with invalid pattern
TEST_F(TerminalTest, CommandProcessing_VolatileCommand_InvalidPattern_ShouldRejectCommand) 
{
    // Arrange
    std::string command = "do ping #@*\n";

    // Expectation: Terminal rejects the command due to invalid IP
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(14);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#do ping #@*\n                    ^\n% Invlid input detected at '^' marker.\n\n");
}

// Test Processing a command with special characters
TEST_F(TerminalTest, CommandProcessing_SpecialCharacters_ShouldRejectCommand) 
{
    // Arrange
    std::string command = "hostname Router@123\n"; // Assuming '@' is invalid

    // Expectation: Terminal rejects the command and prints error
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(22);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(Global::getInstance().getHostname(), "router"); // Hostname should remain default
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#hostname Router@123\n                        ^\n% Invlid input detected at '^' marker.\n\n");
}

#pragma endregion
#pragma region CommandMatching

// Test Matching command with exact case
TEST_F(TerminalTest, MatchingCommands_ExactCase_ShouldMatchSuccessfully) 
{
    // Arrange
    std::string command = "hostname RouterExact";

    // Expectation: Terminal processes the command
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(22);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(Global::getInstance().getHostname(), "RouterExact");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#hostname RouterExact\n");
}

// Test Matching command with different casing (assuming case-insensitive)
TEST_F(TerminalTest, MatchingCommands_DifferentCasing_ShouldMatchSuccessfully) 
{
    // Arrange
    std::string command = "HoStNaMe RouterCase\n";

    // Expectation: Terminal normalizes and processes the command
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(21);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(Global::getInstance().getHostname(), "RouterCase");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#HoStNaMe RouterCase\n");
}

// Test Matching partial command to full command
TEST_F(TerminalTest, MatchingCommands_PartialToFull_ShouldMatchSuccessfully) 
{
    // Arrange
    std::string partialCommand = "host RouterPartial\n";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(20);

    // Act
    bool result = handleInput(partialCommand);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(Global::getInstance().getHostname(), "RouterPartial");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#host RouterPartial\n");
}

// Test Matching command with invalid hierarchy
TEST_F(TerminalTest, MatchingCommands_InvalidHierarchy_ShouldRejectCommand) 
{
    // Arrange
    std::string command = "interface GigabitEthernet 1 ip address 10.0.0.1 255.255.255.0 extraArg\n";

    // Expectation: Terminal rejects the command due to excessive arguments
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(73);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#interface GigabitEthernet 1 ip address 10.0.0.1 255.255.255.0 extraArg\n                                           ^\n% Invlid input detected at '^' marker.\n\n");
}

#pragma endregion
#pragma region Normalization

// Test Normalizing a partial command by filling in required words
TEST_F(TerminalTest, Normalization_FillInRequiredWords_ShouldNormalizeCommand) {
    // Arrange
    std::string partialCommand = "host Router1";
    std::string normalizedCommand = "hostname Router1";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(0);

    // Act
    std::string result = normalizeCommand(partialCommand);

    // Assert
    EXPECT_EQ(result, normalizedCommand);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test Normalizing command with abbreviated subcommands
TEST_F(TerminalTest, Normalization_AbbreviatedSubcommands_ShouldNormalizeCommand) {
    // Arrange
    std::string command = "int Gig 1";
    std::string command2 = "ip ad 10.0.0.1 255.255.255.0";
    std::string normalizedCommand = "interface GigabitEthernet 1";
    std::string normalizedCommand2 = "ip address 10.0.0.1 255.255.255.0";

    std::string subType = "GigabitEthernet";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(0);

    // Act
    std::string result = normalizeCommand(command);
    configureInterfaceMode(subType);
    initialize();
    std::string result2 = normalizeCommand(command2);

    // Assert
    EXPECT_EQ(result, normalizedCommand);
    EXPECT_EQ(result2, normalizedCommand2);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test Normalizing command with mixed case and spaces
TEST_F(TerminalTest, Normalization_MixedCaseAndSpaces_ShouldNormalizeCommand) {
    // Arrange
    std::string command = "  HoStNa   RouterMixedCase  ";
    std::string normalizedCommand = "hostname RouterMixedCase";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(0);

    // Act
    std::string result = normalizeCommand(command);

    // Assert
    EXPECT_EQ(result, normalizedCommand);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

#pragma endregion
#pragma region GlobalCommand

// Test Executing global command 'exit' to leave configuration mode
TEST_F(TerminalTest, GlobalCommand_ExitConfigurationMode_ShouldChangeMode) 
{
    // Arrange
    changeMode(mode.globalConfiguration);
    std::string command = "exit";

    // Expectation: Terminal processes the 'exit' command and changes mode
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(6);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(getCurrentMode(), "#");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#exit\n");
}

// Test Executing global command 'end' to exit to privileged EXEC mode
TEST_F(TerminalTest, GlobalCommand_EndConfigurationMode_ShouldChangeMode) 
{
    // Arrange
    std::string command = "end";

    // Expectation: Terminal processes the 'end' command and changes mode
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(5);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(getCurrentMode(), "#");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#end\n");
}

#pragma endregion
#pragma region InvalidInput

// Test Processing a completely unknown command
TEST_F(TerminalTest, InvalidInput_UnknownCommand_ShouldRejectCommand)
{
    // Arrange
    std::string command = "foobar";

    // Expectation: Terminal rejects the command and prints error
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(9);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#foobar\n               ^\n% Invlid input detected at '^' marker.\n\n");
}

// Test Processing a command with invalid syntax
TEST_F(TerminalTest, InvalidInput_InvalidSyntax_ShouldRejectCommand) 
{
    // Arrange
    std::string command = "interface GigabitEthernet 1 ip address";

    // Expectation: Terminal rejects the command due to missing arguments
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(41);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#interface GigabitEthernet 1 ip address\n                                           ^\n% Invlid input detected at '^' marker.\n\n");
}

// Test Processing a command with invalid characters
TEST_F(TerminalTest, InvalidInput_InvalidCharacters_ShouldRejectCommand) 
{
    // Arrange
    std::string command = "hostname Router!@#";

    // Expectation: Terminal rejects the command due to invalid characters
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(21);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#hostname Router!@#\n                        ^\n% Invlid input detected at '^' marker.\n\n");
}

// Test Processing a command with invalid mode in hierarchy
TEST_F(TerminalTest, InvalidInput_InvalidModeHierarchy_ShouldRejectCommand) 
{
    // Arrange
    std::string command = "router ospf 1 area 0";

    // Expectation: Terminal rejects the command due to invalid mode hierarchy
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(23);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#router ospf 1 area 0\n                             ^\n% Invlid input detected at '^' marker.\n\n");
}

#pragma endregion
#pragma region IPv6Expansion

// Test Expanding a compressed IPv6 address
TEST_F(TerminalTest, IPv6Expanding_CompressedAddress_ShouldExpandCorrectly) 
{
    // Arrange
    std::string compressedIPv6 = "2001:db8::1";
    std::string expectedExpanded = "2001:0db8:0000:0000:0000:0000:0000:0001";

    // Act
    std::string expanded = expandIPv6Address(compressedIPv6);

    // Assert
    EXPECT_EQ(expanded, expectedExpanded);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test Expanding a fully expanded IPv6 address should remain unchanged
TEST_F(TerminalTest, IPv6Expanding_FullyExpandedAddress_ShouldRemainUnchanged) 
{
    // Arrange
    std::string expandedIPv6 = "2001:0db8:85a3:0000:0000:8a2e:0370:7334";
    std::string expectedExpanded = "2001:0db8:85a3:0000:0000:8a2e:0370:7334";

    // Act
    std::string result = expandIPv6Address(expandedIPv6);

    // Assert
    EXPECT_EQ(result, expectedExpanded);
}

// Test Expanding an IPv6 address with multiple "::" should handle error
TEST_F(TerminalTest, IPv6Expanding_MultipleCompressedSections_ShouldHandleError) 
{
    // Arrange
    std::string compressedIPv6 = "2001::85a3::7334";
    std::string expectedError = "Error: Invalid IPv6 address format.\n";
    std::string expectedPrompt = "(config)# ";

    // Act
    std::string expanded = expandIPv6Address(compressedIPv6);

    // Assert
    // Assuming the method returns an empty string on error
    EXPECT_EQ(expanded, "");
}

#pragma endregion
#pragma region BatchProcessing

// Utility function to simulate batch processing and recovery
bool TerminalTest::batchProcessAndRecover(const std::vector<std::string>& commands, const std::vector<std::string>& expectedOutputs, std::vector<std::string>& recoveredCommands) {
    // Execute commands
    for (auto& command : commands) {
        handleInput(command);
    }

    // Mock reading the saved configuration
    EXPECT_CALL(*mockFileSystem, writeFile(STARTUP_FILE, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(true));
    save();

    // Recover state
    nlohmann::ordered_json startup = getRoot();
    recoveredCommands = recoverConfigs(startup);

    bool recoveryValid = true;
    for (size_t i = 0; i < recoveredCommands.size(); ++i)
    {
        if (recoveredCommands[i] != expectedOutputs[i])
        {
            recoveryValid = false;
        }
    }
    if (!recoveredCommands.empty() && recoveredCommands.size() == expectedOutputs.size())
    {
        return recoveryValid;
    }
    return false;
}

// Test Batch processing multiple configuration commands and recovering them accurately
TEST_F(TerminalTest, BatchProcessing_MultipleCommands_ShouldProcessAndRecoverAccurately) 
{
    // Arrange
    std::vector<std::string> commands = {
        "hostname BatchRouter",
        "interface GigabitEthernet 1",
        "ip address 172.16.0.1 255.255.255.0",
        "exit",
    };
    std::vector<std::string> expectedOutputs = {
        "hostname BatchRouter",
        "interface GigabitEthernet 1",
        "ip address 172.16.0.1 255.255.255.0",
        "exit",
    };
    std::vector<std::string> recoveredCommands;

    // Act & Assert
    bool recoveryResult = batchProcessAndRecover(commands, expectedOutputs, recoveredCommands);
    EXPECT_TRUE(recoveryResult);

    // Additional assertions based on internal state
    EXPECT_EQ(Global::getInstance().getHostname(), "BatchRouter");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#hostname BatchRouter\nBatchRouter(config)#interface GigabitEthernet 1\nBatchRouter(config-if)#ip address 172.16.0.1 255.255.255.0\nBatchRouter(config-if)#exit\n");
}

// Test Batch processing with invalid commands should handle errors and continue
TEST_F(TerminalTest, BatchProcessing_InvalidCommands_ShouldHandleErrorsAndContinue) 
{
    // Arrange
    std::vector<std::string> commands = {
        "hostname BatchRouter",
        "invalidcmd",
        "interface GigabitEthernet 1",
        "ip address 10.0.0.1 255.255.255.0",
    };
    std::vector<std::string> expectedOutputs = {
        "hostname BatchRouter",
        "interface GigabitEthernet 1",
        "ip address 10.0.0.1 255.255.255.0",
    };
    std::vector<std::string> recoveredCommands;

    // Act & Assert
    bool recoveryResult = batchProcessAndRecover(commands, expectedOutputs, recoveredCommands);
    EXPECT_TRUE(recoveryResult);

    // Additional assertions based on internal state
    EXPECT_EQ(getCurrentMode(), mode.interface);
    EXPECT_EQ(Global::getInstance().getHostname(), "BatchRouter");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#hostname BatchRouter\nBatchRouter(config)#invalidcmd\n                    ^\n% Invlid input detected at '^' marker.\n\nBatchRouter(config)#interface GigabitEthernet 1\nBatchRouter(config-if)#ip address 10.0.0.1 255.255.255.0\n");
}

// Test Executing a comprehensive list of valid commands and verifying state
TEST_F(TerminalTest, ComprehensiveConfiguration_ValidCommands_ShouldUpdateStateCorrectly) 
{
    // Arrange
    std::vector<std::string> commands = {
        "hostname ComprehensiveRouter",
        "interface GigabitEthernet 1",
        "ip address 192.168.1.1 255.255.255.0",
        "no shutdown",
        "exit",
        "router ospf 1",
        "network 192.168.1.0 0.0.0.255 area 0",
        "exit"
    };
    std::vector<std::string> expectedOutputs = {
        "hostname ComprehensiveRouter",
        "interface GigabitEthernet 1",
        "ip address 192.168.1.1 255.255.255.0",
        "no shutdown",
        "exit",
        "router ospf 1",
        "network 192.168.1.0 0.0.0.255 area 0",
        "exit"
    };
    std::vector<std::string> recoveredCommands;

    // Act & Assert
    bool recoveryResult = batchProcessAndRecover(commands, expectedOutputs, recoveredCommands);
    EXPECT_TRUE(recoveryResult);

    // Additional assertions based on internal state
    EXPECT_EQ(getCurrentMode(), mode.globalConfiguration);
    EXPECT_EQ(Global::getInstance().getHostname(), "ComprehensiveRouter");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#hostname ComprehensiveRouter\nComprehensiveRouter(config)#interface GigabitEthernet 1\nComprehensiveRouter(config-if)#ip address 192.168.1.1 255.255.255.0\nComprehensiveRouter(config-if)#no shutdown\nComprehensiveRouter(config-if)#exit\nComprehensiveRouter(config)#router ospf 1\nComprehensiveRouter(config-router)#network 192.168.1.0 0.0.0.255 area 0\nComprehensiveRouter(config-router)#exit\n");
}

// Test Recovering state after a series of commands
TEST_F(TerminalTest, StateRecovery_AfterSeriesOfCommands_ShouldRestoreCorrectly) 
{
    // Arrange
    std::vector<std::string> commands = {
        "hostname RecoverRouter",
        "interface GigabitEthernet 1",
        "ip address 10.0.0.1 255.255.255.0",
        "invalidcmd",
        "exit",
        "router ospf 1",
        "network 10.0.0.0 0.0.0.255 area 0",
        "exit",
    };
    std::vector<std::string> expectedOutputs = {
        "hostname RecoverRouter",
        "interface GigabitEthernet 1",
        "ip address 10.0.0.1 255.255.255.0",
        "exit",
        "router ospf 1",
        "network 10.0.0.0 0.0.0.255 area 0",
        "exit"
    };
    std::vector<std::string> recoveredCommands;

    // Act & Assert
    bool recoveryResult = batchProcessAndRecover(commands, expectedOutputs, recoveredCommands);
    EXPECT_TRUE(recoveryResult);

    // Additional assertions based on internal state
    EXPECT_EQ(getCurrentMode(), mode.globalConfiguration);
    EXPECT_EQ(Global::getInstance().getHostname(), "RecoverRouter");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#hostname RecoverRouter\nRecoverRouter(config)#interface GigabitEthernet 1\nRecoverRouter(config-if)#ip address 10.0.0.1 255.255.255.0\nRecoverRouter(config-if)#invalidcmd\n                         ^\n% Invlid input detected at '^' marker.\n\nRecoverRouter(config-if)#exit\nRecoverRouter(config)#router ospf 1\nRecoverRouter(config-router)#network 10.0.0.0 0.0.0.255 area 0\nRecoverRouter(config-router)#exit\n");
}

// Test Batch processing with abbreviated and invalid commands
TEST_F(TerminalTest, BatchProcessing_MixedValidAndInvalidCommands_ShouldHandleAppropriately)
{
    // Arrange
    std::vector<std::string> commands = {
        "host RecoverRouter",
        "interf Gig 1",
        "ip add 10.0.0.1 255.255.255.0",
        "invalidcmd",
        "exit",
        "router osp 1",
        "netw 10.0.0.0 0.0.0.255 are 0",
        "exit",
    };
    std::vector<std::string> expectedOutputs = {
        "hostname RecoverRouter",
        "interface GigabitEthernet 1",
        "ip address 10.0.0.1 255.255.255.0",
        "exit",
        "router ospf 1",
        "network 10.0.0.0 0.0.0.255 area 0",
        "exit"
    };
    std::vector<std::string> recoveredCommands;

    // Act & Assert
    bool recoveryResult = batchProcessAndRecover(commands, expectedOutputs, recoveredCommands);
    EXPECT_TRUE(recoveryResult);

    // Additional assertions based on internal state
    EXPECT_EQ(getCurrentMode(), mode.globalConfiguration);
    EXPECT_EQ(Global::getInstance().getHostname(), "RecoverRouter");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#host RecoverRouter\nRecoverRouter(config)#interf Gig 1\nRecoverRouter(config-if)#ip add 10.0.0.1 255.255.255.0\nRecoverRouter(config-if)#invalidcmd\n                         ^\n% Invlid input detected at '^' marker.\n\nRecoverRouter(config-if)#exit\nRecoverRouter(config)#router osp 1\nRecoverRouter(config-router)#netw 10.0.0.0 0.0.0.255 are 0\nRecoverRouter(config-router)#exit\n");
}

// 21. Additional Helper and Utility Tests
#pragma endregion

// Test 21.3: Checking if a string is numeric
TEST_F(TerminalTest, Utility_IsNumeric_ShouldIdentifyNumericStrings) {
    // Arrange & Act & Assert
    EXPECT_TRUE(isNumeric("12345"));
    EXPECT_TRUE(isNumeric("-6789"));
    EXPECT_FALSE(isNumeric("12a45"));
    EXPECT_FALSE(isNumeric("abcde"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.4: Checking if a string is a valid MAC address
TEST_F(TerminalTest, Utility_IsMACAddress_ShouldValidateCorrectly) {
    // Arrange & Act & Assert
    EXPECT_TRUE(isMACAddress("00:1A:2B:3C:4D:5E"));
    EXPECT_TRUE(isMACAddress("00-1A-2B-3C-4D-5E"));
    EXPECT_FALSE(isMACAddress("001A.2B3C.4D5E"));
    EXPECT_FALSE(isMACAddress("00:1A:2B:3C:4D"));
    EXPECT_FALSE(isMACAddress("GG:HH:II:JJ:KK:LL"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.5: Checking if a string is a valid IPv6 address
TEST_F(TerminalTest, Utility_IsIPv6Address_ShouldValidateCorrectly) {
    // Arrange & Act & Assert
    EXPECT_TRUE(isIPv6Address("2001:0db8:85a3:0000:0000:8a2e:0370:7334"));
    EXPECT_TRUE(isIPv6Address("2001:db8::1"));
    EXPECT_FALSE(isIPv6Address("2001:0db8:85a3::8a2e:0370:7334:"));
    EXPECT_FALSE(isIPv6Address("2001:0db8:85a3:0000:0000:8a2e:0370"));
    EXPECT_FALSE(isIPv6Address("2001:0db8:85a3:0000:0000:8a2e:0370:7334:1234"));
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.8: Expanding IPv6 address correctly
TEST_F(TerminalTest, IPv6Expanding_ValidCompressedAddress_ShouldExpandSuccessfully) {
    // Arrange
    std::string compressedIPv6 = "2001:db8::1";
    std::string expectedExpanded = "2001:0db8:0000:0000:0000:0000:0000:0001";

    // Act
    std::string expanded = expandIPv6Address(compressedIPv6);

    // Assert
    EXPECT_EQ(expanded, expectedExpanded);
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.9: Expanding IPv6 address with multiple '::' should handle error
TEST_F(TerminalTest, IPv6Expanding_InvalidCompressedAddress_ShouldHandleError) {
    // Arrange
    std::string compressedIPv6 = "2001::db8::1";

    // Expectation: Terminal rejects the invalid IPv6 address
    EXPECT_CALL(*mockConsole, print(::testing::_)).Times(0);

    // Act
    std::string expanded = expandIPv6Address(compressedIPv6);

    // Assert
    EXPECT_EQ(expanded, "");
    
    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

#pragma endregion
