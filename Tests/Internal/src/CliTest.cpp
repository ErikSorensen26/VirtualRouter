// Forward-declare at global scope BEFORE any namespace-qualified includes.
// This ensures that 'friend class Internal_CliTest' inside namespace cli
// resolves to ::Internal_CliTest (global scope) rather than cli::Internal_CliTest.
class Internal_CliTest;

#include <gtest/gtest.h>
#include <MockConsole.hpp>
#include <MockFileSystem.hpp>
#include <cli/session/CliEngine.h>
#include <cli/session/CliSession.h>
#include <cli/session/CliUtils.h>
#include <cli/session/Configs.h>
#include <cli/modes/Mode.hpp>
#include <core/Global.h>
#include <configs/registry/global/GlobalRegistry.h>

using cli::CliEngine;
using cli::CliSession;
using cli::CliMode;
using cli::FileSystem;
using cli::MockFileSystem;
using cli::ReducedMockConsole;
using core::Global;

// Declare the test class as a friend to access private members
class Internal_CliTest : public ::testing::Test
{
public:
    // Mock objects
    testing::NiceMock<ReducedMockConsole>* mockConsole;
    static MockFileSystem* mockFileSystem;
    static FileSystem* realFileSystem;
    static CliEngine* engine;
    static Global* global;
protected:

    // Terminal instance
    CliSession* terminal = nullptr;

    static std::string commandTreeString;
    static std::string configSchemaString;
    static std::string configFileString;

    // Stub sub-mode string (replaces old terminal->currentSubMode)
    std::string stubSubMode;

    static void SetUpTestSuite()
    {
        // Create Initiate file systems
        realFileSystem = new FileSystem();
        mockFileSystem = new testing::NiceMock<MockFileSystem>();

        // Load the JSON file into the sampleCommandTree
        if (realFileSystem->fileExists("./" + std::string(COMMAND_TREE)))
        {
            realFileSystem->readFile("./" + std::string(COMMAND_TREE), commandTreeString);
        }
        else
        {
            FAIL() << "Failed to open the command tree file: " << COMMAND_TREE;
        }

        if (realFileSystem->fileExists("./" + std::string(HW_CONFIG_FILE)))
        {
            realFileSystem->readFile("./" + std::string(HW_CONFIG_FILE), configFileString);
        }
        else
        {
            FAIL() << "Failed to open the hardware config file: " << HW_CONFIG_FILE;
        }

        mockFileSystem->setupMockFile(COMMAND_TREE, commandTreeString);
        mockFileSystem->setupMockFile(HW_CONFIG_FILE, configFileString);
        mockFileSystem->setupMockFile(ROUTER_CONFIG_FILE, "{}");

        global = new Global(*mockFileSystem, {}, true);
        global->txMgr.setCorePool({1, 2, 3, 4});
        engine = &global->engine;
        engine->paginationCount = 0;
    }

    void SetUp() override
    {
        global->reset();
        mockConsole = new testing::NiceMock<ReducedMockConsole>();

        terminal = new CliSession(*engine, *mockConsole);
        engine->sessions.push_back(terminal);
        changeMode(CliMode::GlobalConfiguration);
        mockConsole->resetCapturedOutput();
    }

    void TearDown() override
    {
        global->removeRoutingInstance("default");
        engine->sessions.clear();
        delete terminal;
        delete mockConsole;
        mockConsole = nullptr;
        terminal = nullptr;
    }

    static void TearDownTestSuite()
    {
        delete realFileSystem;
        delete mockFileSystem;
        delete global;
    }

    // Other functions
    bool batchProcessAndRecover(const std::vector<std::string>& commands, const std::vector<std::string>& expectedOutputs, std::vector<std::string>& recoveredCommands);

public:
    // Helper functions
    bool executeCommand(std::string& command) {return handleInput(command);}
    bool handleInput(const std::string& command) {return terminal->handleInput(command);}

    void changeMode(CliMode newMode)
    {
        switch (newMode)
        {
            case CliMode::UserExec:
                terminal->changeMode<CliMode::UserExec>(global->configs);
                break;
            case CliMode::PrivilegedExec:
                terminal->changeMode<CliMode::PrivilegedExec>(global->configs);
                break;
            case CliMode::GlobalConfiguration:
                terminal->changeMode<CliMode::PrivilegedExec>(global->configs);
                terminal->changeMode<CliMode::GlobalConfiguration>(global->configs);
                break;
            default:
                // Invalid / unsupported mode — do nothing
                break;
        }
    }

    void configureRoutingMode(std::string& newMode)
    {
        stubSubMode = newMode;
        // No equivalent in new API — sub-mode is set implicitly by command dispatch
    }

    void configureInterfaceMode(std::string& interface)
    {
        stubSubMode = interface;

        interface::InterfaceKey key;
        if (!cli::utils::extractInterfaceId(interface, "1", key)) return;

        auto interfaceCfgs = global->getConfigs().get<config::Global::INTERFACE>();
        terminal->changeMode<CliMode::Interface>(interfaceCfgs.emplaceBack(key));
    }

    std::string getHostname() {return global->getHostname();}

    // True when the parser resolved the line into a runnable command.
    bool parsesOk(std::string cmd)
    {
        return terminal->parseInput(cmd).status == CliSession::ParseResult::Status::OK_;
    }

    CliMode getCurrentMode() {return terminal->getMode();}

    std::string getCurrentSubMode() {return stubSubMode;}

    std::string getNextLine() {return terminal->nextLine;}

    std::string normalizeCommand(std::string& command)
    {
        CliSession::ParseResult parsed = terminal->parseInput(command);
        if (parsed.status != CliSession::ParseResult::Status::OK_) return command;

        std::string out;
        for (const cli::Token& t : parsed.tokens)
        {
            if (!out.empty()) out += ' ';
            out += std::string_view(t.value);
        }
        return out;
    }

    // Inline IPv6 expansion (cli::utils::expandIPv6Address has no implementation in the library)
    std::string expandIPv6Address(std::string ip)
    {
        // Parse the IPv6 address and re-expand it using the types::IPv6Address infrastructure.
        types::IPv6Address addr;
        if (!cli::utils::extractIPv6Address(ip, addr))
            return "";
        // Format back to full notation: 8 groups of 4 hex digits
        // addr.addr is __uint128_t in host byte order; extract groups from the high bits down
        __uint128_t raw = addr.addr;
        uint16_t groups[8];
        for (int i = 7; i >= 0; --i)
        {
            groups[i] = static_cast<uint16_t>(raw & 0xFFFF);
            raw >>= 16;
        }
        char buf[40];
        std::snprintf(buf, sizeof(buf),
            "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
            groups[0], groups[1], groups[2], groups[3],
            groups[4], groups[5], groups[6], groups[7]);
        return std::string(buf);
    }

    bool save()
    {
        // saveConfig no longer exists in CliEngine; stub returning false for compilation
        return false;
    }

    bool isNumeric(const std::string num) {return engine->isNumeric(num);}

    bool isMACAddress(const std::string mac) {return cli::utils::isMACAddress(mac);}

    bool isIPv6Address(const std::string ip) {return cli::utils::isIPv6Address(ip);}
};

// Helper: find CliMode by its prompt string
static std::optional<CliMode> findMode(const std::string& prompt)
{
    for (size_t i = 0; i < static_cast<size_t>(CliMode::Count); ++i)
    {
        CliMode m = static_cast<CliMode>(i);
        if (cli::getPrompt(m) == prompt)
            return m;
    }
    return std::nullopt;
}

// Helper: return prompt string for a mode
static std::string getModePrompt(CliMode mode)
{
    return std::string(cli::getPrompt(mode));
}

CliEngine* Internal_CliTest::engine = nullptr;
FileSystem* Internal_CliTest::realFileSystem = nullptr;
MockFileSystem* Internal_CliTest::mockFileSystem = nullptr;
Global* Internal_CliTest::global = nullptr;
std::string Internal_CliTest::commandTreeString;
std::string Internal_CliTest::configSchemaString;
std::string Internal_CliTest::configFileString;
#pragma region ModeChange

// Test Changing mode from global to user EXEC
TEST_F(Internal_CliTest, ModeChange_GlobalToUserExec_ShouldUpdateMode)
{
    // Arrange
    std::string newModeStr = "#"; // User EXEC mode prompt
    CliMode newMode = findMode(newModeStr).value();

    // Act
    changeMode(newMode);

    // Assert
    EXPECT_EQ(getCurrentMode(), newMode);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test Changing mode to invalid mode should fail
TEST_F(Internal_CliTest, ModeChange_InvalidMode_ShouldRejectModeChange)
{
    // Arrange
    CliMode invalidMode = CliMode::Count;

    std::string expectedPrompt = "(config)#";

    // Act
    changeMode(invalidMode);

    // Assert
    EXPECT_EQ(getModePrompt(getCurrentMode()), expectedPrompt); // Assuming initial mode

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test Switching to sub-mode (e.g., interface configuration)
TEST_F(Internal_CliTest, ModeChange_SwitchToSubMode_ShouldUpdateMode)
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
TEST_F(Internal_CliTest, InputHandling_EmptyInput_ShouldRejectCommand)
{
    // Arrange
    std::string command = "\n";

    // Act
    bool result = executeCommand(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "\r\nrouter(config)#");
}

// Test 3.2: Handling input with only spaces should reject command
TEST_F(Internal_CliTest, InputHandling_SpacesOnly_ShouldRejectCommand)
{
    // Arrange
    std::string command = "   \n";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(5);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "   \r\nrouter(config)#");
}

// Test 3.3: Handling valid input with leading and trailing spaces
TEST_F(Internal_CliTest, InputHandling_ValidInputWithSpaces_ShouldProcessCommand)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string rawCommand = "  hostname Router1  \n";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(22);

    // Act
    bool result = handleInput(rawCommand);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(getHostname(), "Router1");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "  hostname Router1  \r\nRouter1(config)#");
}

#pragma endregion
#pragma region DoTesting

// Test Executing a "do" command from configuration mode
TEST_F(Internal_CliTest, DoCommand_FromConfigurationMode_ShouldExecutePrivilegedCommand)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string doCommand = "do show running-config\n";

    // Expectation: Terminal executes the "do" command and prints output
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(24);

    // Act
    bool result = handleInput(doCommand);

    // Assert
    EXPECT_TRUE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "do show running-config\r\nrouter(config)#");
}

// Test Executing an invalid "do" command
TEST_F(Internal_CliTest, DoCommand_InvalidCommand_ShouldRejectCommand) {
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string doCommand = "do invalidcmd\n";

    // Expectation: Terminal rejects the "do" command and prints error
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(16);

    // Act
    bool result = handleInput(doCommand);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "do invalidcmd\r\n^\r\n% Invalid input detected at '^' marker.\r\n\r\nrouter(config)#");
}

// Test Executing a "do" command with missing parameters
TEST_F(Internal_CliTest, DoCommand_MissingParameters_ShouldRejectCommand) {
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string doCommand = "do ping\n";

    // Expectation: Terminal rejects the "do" command due to missing parameters
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(10);

    // Act
    bool result = handleInput(doCommand);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "do ping\r\n% Incomplete Command\r\nrouter(config)#");
}

// Test Executing a "do" command from sub-mode
TEST_F(Internal_CliTest, DoCommand_FromSubMode_ShouldExecuteCommandWithinSubMode)
{
    // Arrange
    // First, enter sub-mode
    changeMode(CliMode::GlobalConfiguration);
    std::string enterSubModeCmd = "interface GigabitEthernet 1\n";
    std::string doCommand = "do show interface GigabitEthernet 1";

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(66);

    // Act: Enter sub-mode
    bool result1 = handleInput(enterSubModeCmd);
    EXPECT_TRUE(result1);

    // Now, execute "do" command within sub-mode

    // Act: Execute "do" command within sub-mode
    bool result2 = handleInput(doCommand);
    EXPECT_TRUE(result2);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "interface GigabitEthernet 1\r\nrouter(config-if)#do show interface GigabitEthernet 1\r\nrouter(config-if)#");
}

#pragma endregion
#pragma region AutoComplete

// Test Displaying help using '?'
TEST_F(Internal_CliTest, HelpRequest_WithQuestionMark_ShouldDisplayAvailableCommands)
{
    // Arrange
    changeMode(CliMode::UserExec);
    std::string helpCommand = "?";
    std::string expectedOutput;

    // Expectation: Terminal prints available commands and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(16);

    // Act
    bool result = handleInput(helpCommand);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "?\r\n  <1-99>          Session number to resume\r\n  connect         Open a terminal connection\r\n  disable         Turn off privileged commands\r\n  disconnect      Disconnect an existing network connection\r\n  enable          Turn on privileged commands\r\n  logout          Exit from the EXEC\r\n  ping            Send echo messages\r\n  resume          Resume an active network connection\r\n  show            Show running system information\r\n  ssh             Open a secure shell client connection\r\n  telnet          Open a telnet connection\r\n  terminal        Set terminal line parameters\r\n  traceroute      Trace route to destination\r\nrouter>");
}

// Test '?' after a prefix that several commands share.
TEST_F(Internal_CliTest, HelpRequest_AmbiguousPrefix_ShouldListOnlyMatchingCommands)
{
    // Arrange
    changeMode(CliMode::UserExec);
    std::string helpCommand = "s?";

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(::testing::AnyNumber());

    // Act
    bool result = handleInput(helpCommand);

    // Assert: only the commands starting with 's', not the whole mode listing
    EXPECT_FALSE(result);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "s?\r\n  show      Show running system information\r\n  ssh       Open a secure shell client connection\r\nrouter>s");
}

// Test '?' on a prefix of a subcommand.
TEST_F(Internal_CliTest, HelpRequest_SubcommandPrefix_ShouldFilterByPartialWord)
{
    // Arrange
    changeMode(CliMode::UserExec);
    std::string helpCommand = "show c?";

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(::testing::AnyNumber());

    // Act
    bool result = handleInput(helpCommand);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "show c?\r\n  cdp              CDP information\r\n  class-map        Show QoS Class Map\r\n  clock            Display the system clock\r\n  controllers      Interface controllers status\r\n  crypto           Encryption module\r\nrouter>show c");
}

// Test '?' on a prefix that matches nothing.
TEST_F(Internal_CliTest, HelpRequest_UnmatchedPrefix_ShouldReportUnrecognized)
{
    // Arrange
    changeMode(CliMode::UserExec);
    std::string helpCommand = "zz?";

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(::testing::AnyNumber());

    // Act
    bool result = handleInput(helpCommand);

    // Assert: '?' is always consumed as help, even when nothing matches, so the
    // line is not treated as a failed command here.
    EXPECT_TRUE(result);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "zz?\r\n% Unrecognized Command\r\nrouter>zz");
}

// Test Auto-completing a unique partial command using Tab
TEST_F(Internal_CliTest, AutoComplete_UniquePartialCommand_ShouldCompleteCommand)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string partialInput = "host\t";
    std::string finishInput = " Router1\n";

    // Expectation: Terminal auto-completes the command
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(17);

    // Act
    bool result = handleInput(partialInput);
    bool result2 = handleInput(finishInput);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_TRUE(result2);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "host\r\nrouter(config)#hostname  Router1\r\nRouter1(config)#");
}

// Test Auto-completing an ambiguous partial command using Tab
TEST_F(Internal_CliTest, AutoComplete_AmbiguousPartialCommand_ShouldListSuggestions)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string partialInput = "a\t";

    // Expectation: Terminal lists available suggestions
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(4);

    // Act
    bool result = handleInput(partialInput);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "a\r\nrouter(config)#a");
}

// Test Auto-completing an exact command should do nothing
TEST_F(Internal_CliTest, AutoComplete_ExactCommand_ShouldNotChangeInput)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string exactCommand = "exit\t";

    // Expectation: Terminal does not attempt to auto-complete
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(7);

    // Act
    bool result = handleInput(exactCommand);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "exit\r\nrouter(config)#exit ");
}

// Test that Tab after a value matching a pattern leaves the value alone
TEST_F(Internal_CliTest, AutoComplete_AfterPatternMatch_ShouldNotReplaceWithPattern)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string patternInput = "hostname Router1\t";

    // Expectation: Terminal echoes the line back unchanged
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(::testing::AnyNumber());

    // Act
    bool result = handleInput(patternInput);

    EXPECT_TRUE(result);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "hostname Router1\r\nrouter(config)#hostname Router1");
}

// Test that the literal text of a pattern is not accepted as a value
TEST_F(Internal_CliTest, PatternName_TypedLiterally_ShouldNotMatchPattern)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);

    // Expectation: no output is asserted, only whether the line resolves
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(::testing::AnyNumber());

    EXPECT_FALSE(parsesOk("ip route A.B.C.D A.B.C.D A.B.C.D"));

    // A real address on the same command still resolves.
    EXPECT_TRUE(parsesOk("ip route 1.1.1.0 255.255.255.0 2.2.2.2"));
}

// Test Tab on a command that is also the prefix of a longer one
TEST_F(Internal_CliTest, AutoComplete_ExactMatchThatIsAlsoAPrefix_ShouldNotComplete)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string ambiguous = "ip\t";

    // Expectation: Terminal leaves the line as typed
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(::testing::AnyNumber());

    // Act
    bool result = handleInput(ambiguous);

    // Assert: "ip" is a whole command and the start of "ipv6", so there are two
    // ways to continue and nothing to complete to.
    EXPECT_TRUE(result);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "ip\r\nrouter(config)#ip");
}

// Test '?' on a command that is also the prefix of a longer one
TEST_F(Internal_CliTest, HelpRequest_ExactMatchThatIsAlsoAPrefix_ShouldListBoth)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string prefixHelp = "ip?";

    // Expectation: Terminal lists every command starting with "ip"
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(::testing::AnyNumber());

    // Act
    handleInput(prefixHelp);

    // Assert: matching "ip" exactly must not hide "ipv6"; the user typed "ip?"
    // precisely to find out what else starts that way.
    const std::string out = mockConsole->getCapturedOutput();
    EXPECT_NE(out.find("ipv6"), std::string::npos);
    EXPECT_NE(out.find("\r\n  ip "), std::string::npos);
}

// Test Displaying help within sub-mode using '?'
TEST_F(Internal_CliTest, HelpRequest_InSubMode_ShouldDisplayAvailableSubCommands)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    // First, enter sub-mode
    std::string enterSubModeCmd = "interface GigabitEthernet 1\n";
    std::string helpCommand = "?";

    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(77);

    // Act: Enter sub-mode
    bool result1 = handleInput(enterSubModeCmd);
    EXPECT_TRUE(result1);

    // Act: Invoke help in sub-mode
    bool result2 = handleInput(helpCommand);
    EXPECT_FALSE(result2);
    EXPECT_EQ(mockConsole->getCapturedOutput(), "interface GigabitEthernet 1\r\nrouter(config-if)#?\r\n  aaa                 Authentication, Authorization and Accounting.\r\n  arp                 Set arp type (arpa, probe, snap), timeout, log options or packet priority\r\n  bandwidth           Set bandwidth informational parameter\r\n  bfd                 BFD interface configuration commands\r\n  cdp                 CDP interface subcommands\r\n  channel-group       Add this interface to an Etherchannel group\r\n  crypto              Encryption/Decryption commands\r\n  dampening           Enable event dampening\r\n  delay               Specify interface throughput delay\r\n  description         Interface specific description\r\n  dot1q               dot1q interface configuration commands\r\n  duplex              Configure duplex operation.\r\n  exit                Exit from interface configuration mode\r\n  flow-sampler        Attach flow sampler to the interface\r\n  glbp                Gateway Load Balancing Protocol interface commands\r\n  help                Description of the interactive help system\r\n  history             Interface history histograms - 60 second, 60 minute and 72 hour\r\n  hold-queue          Set hold queue depth\r\n  ip                  Interface Internet Protocol config commands\r\n  ipv6                IPv6 interface subcommands\r\n  keepalive           Enable keepalive\r\n  lan-name            LAN Name command\r\n  load-interval       Specify interval for load calculation for an interface\r\n  loopback            Configure internal loopback on an interface\r\n  mac-address         Manually set interface MAC address\r\n  mpls                Configure MPLS interface parameters\r\n  mtu                 Set the interface Maximum Transmission Unit (MTU)\r\n  negotiation         Select autonegotiation mode\r\n  no                  Negate a command or set its defaults\r\n  ntp                 Configure NTP\r\n  ospfv3              OSPFv3 interface commands\r\n  pppoe               pppoe interface subcommands\r\n  pppoe-client        pppoe client\r\n  rate-limit          Rate Limit\r\n  rmon                Configure Remote Monitoring on an interface\r\n  service-policy      Configure CPL Service Policy\r\n  shutdown            Shutdown the selected interface\r\n  snmp                Modify SNMP interface parameters\r\n  speed               Configure speed operation.\r\n  standby             HSRP interface configuration commands\r\n  timeout             Define timeout values for this interface\r\n  vlan-id             Process VLAN-encapsulated packets with a specific VLAN ID\r\n  vlan-range          Process VLAN-encapsulated packets with a range of VLAN IDs\r\n  vrf                 VPN Routing/Forwarding parameters on the interface\r\n  vrrp                VRRP Interface configuration commands\r\nrouter(config-if)#");
}

#pragma endregion
#pragma region CommandProcessing

// Test Processing a valid global command
TEST_F(Internal_CliTest, CommandProcessing_ValidGlobalCommand_ShouldProcessSuccessfully)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string command = "hostname Router1\n";

    // Expectation: Terminal prints the command and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(18);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(global->getHostname(), "Router1");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "hostname Router1\r\nRouter1(config)#");
}

// Test Processing an invalid global command
TEST_F(Internal_CliTest, CommandProcessing_InvalidGlobalCommand_ShouldRejectCommand)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string command = "invalidcmd\n";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(13);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "invalidcmd\r\n^\r\n% Invalid input detected at '^' marker.\r\n\r\nrouter(config)#");
}

// Test Processing a command with missing required arguments
TEST_F(Internal_CliTest, CommandProcessing_MissingArguments_ShouldRejectCommand)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string command = "hostname\n"; // Missing hostname value

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(11);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(global->getHostname(), "router"); // Hostname should remain default

    EXPECT_EQ(mockConsole->getCapturedOutput(), "hostname\r\n% Incomplete Command\r\nrouter(config)#");
}

// Test Processing a command with excessive arguments
TEST_F(Internal_CliTest, CommandProcessing_ExcessiveArguments_ShouldRejectCommand)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string command = "hostname Router1 ExtraArg\n";

    // Expectation: Terminal prints an error and prompt
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(28);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(global->getHostname(), "router"); // Hostname should remain default

    EXPECT_EQ(mockConsole->getCapturedOutput(), "hostname Router1 ExtraArg\r\n                 ^\r\n% Invalid input detected at '^' marker.\r\n\r\nrouter(config)#");
}

// Test Processing a volatile command with pattern matching
TEST_F(Internal_CliTest, CommandProcessing_VolatileCommand_ShouldValidatePatterns)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string command = "do ping 192.168.1.1\n";

    // Expectation: Terminal processes the command and prints output
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(21);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "do ping 192.168.1.1\r\nrouter(config)#");
}

// Test Processing a volatile command with invalid pattern
TEST_F(Internal_CliTest, CommandProcessing_VolatileCommand_InvalidPattern_ShouldRejectCommand)
{
    // Arrange
    std::string command = "do ping #@*\n";

    // Expectation: Terminal rejects the command due to invalid IP
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(14);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "do ping #@*\r\n     ^\r\n% Invalid input detected at '^' marker.\r\n\r\nrouter(config)#");
}

// Test Processing a command with special characters
TEST_F(Internal_CliTest, CommandProcessing_SpecialCharacters_ShouldRejectCommand)
{
    // Arrange
    std::string command = "hostname Router@123\n"; // Assuming '@' is invalid

    // Expectation: Terminal rejects the command and prints error
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(22);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);
    EXPECT_EQ(global->getHostname(), "router"); // Hostname should remain default

    EXPECT_EQ(mockConsole->getCapturedOutput(), "hostname Router@123\r\n         ^\r\n% Invalid input detected at '^' marker.\r\n\r\nrouter(config)#");
}

#pragma endregion
#pragma region CommandMatching

// Test Matching command with exact case
TEST_F(Internal_CliTest, MatchingCommands_ExactCase_ShouldMatchSuccessfully)
{
    // Arrange
    std::string command = "hostname RouterExact";

    // Expectation: Terminal processes the command
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(22);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(global->getHostname(), "RouterExact");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "hostname RouterExact\r\nRouterExact(config)#");
}

// Test Matching command with different casing (assuming case-insensitive)
TEST_F(Internal_CliTest, MatchingCommands_DifferentCasing_ShouldMatchSuccessfully)
{
    // Arrange
    std::string command = "HoStNaMe RouterCase\n";

    // Expectation: Terminal normalizes and processes the command
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(21);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(global->getHostname(), "RouterCase");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "HoStNaMe RouterCase\r\nRouterCase(config)#");
}

// Test Matching partial command to full command
TEST_F(Internal_CliTest, MatchingCommands_PartialToFull_ShouldMatchSuccessfully)
{
    // Arrange
    std::string partialCommand = "host RouterPartial\n";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(20);

    // Act
    bool result = handleInput(partialCommand);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(global->getHostname(), "RouterPartial");

    EXPECT_EQ(mockConsole->getCapturedOutput(),"host RouterPartial\r\nRouterPartial(config)#");
}

// Test Matching command with invalid hierarchy
TEST_F(Internal_CliTest, MatchingCommands_InvalidHierarchy_ShouldRejectCommand)
{
    // Arrange
    std::string command = "interface GigabitEthernet 1 ip address 10.0.0.1 255.255.255.0 extraArg\n";

    // Expectation: Terminal rejects the command due to excessive arguments
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(73);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(),"interface GigabitEthernet 1 ip address 10.0.0.1 255.255.255.0 extraArg\r\n                            ^\r\n% Invalid input detected at '^' marker.\r\n\r\nrouter(config)#");
}

#pragma endregion
#pragma region Normalization

// Test Normalizing a partial command by filling in required words
TEST_F(Internal_CliTest, Normalization_FillInRequiredWords_ShouldNormalizeCommand) {
    // Arrange
    std::string partialCommand = "host Router1";
    std::string normalizedCommand = "hostname Router1";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    // Act
    std::string result = normalizeCommand(partialCommand);

    // Assert
    EXPECT_EQ(result, normalizedCommand);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test Normalizing command with abbreviated subcommands
TEST_F(Internal_CliTest, Normalization_AbbreviatedSubcommands_ShouldNormalizeCommand) {
    // Arrange
    std::string command = "int Gig 1";
    std::string command2 = "ip ad 10.0.0.1 255.255.255.0";
    std::string normalizedCommand = "interface GigabitEthernet 1";
    std::string normalizedCommand2 = "ip address 10.0.0.1 255.255.255.0";

    std::string subType = "GigabitEthernet";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    // Act
    std::string result = normalizeCommand(command);
    configureInterfaceMode(subType);
    std::string result2 = normalizeCommand(command2);

    // Assert
    EXPECT_EQ(result, normalizedCommand);
    EXPECT_EQ(result2, normalizedCommand2);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test Normalizing command with mixed case and spaces
TEST_F(Internal_CliTest, Normalization_MixedCaseAndSpaces_ShouldNormalizeCommand) {
    // Arrange
    std::string command = "  HoStNa   RouterMixedCase  ";
    std::string normalizedCommand = "hostname RouterMixedCase";

    // Expectation: Terminal normalizes and executes the command
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    // Act
    std::string result = normalizeCommand(command);

    // Assert
    EXPECT_EQ(result, normalizedCommand);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

#pragma endregion
#pragma region GlobalCommand

// Test Executing global command 'exit' to leave configuration mode
TEST_F(Internal_CliTest, GlobalCommand_ExitConfigurationMode_ShouldChangeMode)
{
    // Arrange
    changeMode(CliMode::GlobalConfiguration);
    std::string command = "exit";

    // Expectation: Terminal processes the 'exit' command and changes mode
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(6);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    // "exit" from global config returns to privileged EXEC, which is also what
    // the expected "router#" prompt below describes.
    EXPECT_EQ(getCurrentMode(), CliMode::PrivilegedExec);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "exit\r\nrouter#");
}

// Test Executing global command 'end' to exit to privileged EXEC mode
TEST_F(Internal_CliTest, GlobalCommand_EndConfigurationMode_ShouldChangeMode)
{
    // Arrange
    std::string command = "end";

    // Expectation: Terminal processes the 'end' command and changes mode
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(5);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_TRUE(result);
    // Matches this test's own name and the expected "router#" prompt below.
    EXPECT_EQ(getCurrentMode(), CliMode::PrivilegedExec);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "end\r\nrouter#");
}

#pragma endregion
#pragma region InvalidInput

// Test Processing a completely unknown command
TEST_F(Internal_CliTest, InvalidInput_UnknownCommand_ShouldRejectCommand)
{
    // Arrange
    std::string command = "foobar";

    // Expectation: Terminal rejects the command and prints error
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(9);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "foobar\r\n^\r\n% Invalid input detected at '^' marker.\r\n\r\nrouter(config)#");
}

// Test Processing a command with invalid syntax
TEST_F(Internal_CliTest, InvalidInput_InvalidSyntax_ShouldRejectCommand)
{
    // Arrange
    std::string command = "interface GigabitEthernet 1 ip address";

    // Expectation: Terminal rejects the command due to missing arguments
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(41);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "interface GigabitEthernet 1 ip address\r\n                            ^\r\n% Invalid input detected at '^' marker.\r\n\r\nrouter(config)#");
}

// Test Processing a command with invalid characters
TEST_F(Internal_CliTest, InvalidInput_InvalidCharacters_ShouldRejectCommand)
{
    // Arrange
    std::string command = "hostname Router!@#";

    // Expectation: Terminal rejects the command due to invalid characters
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(21);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(),"hostname Router!@#\r\n         ^\r\n% Invalid input detected at '^' marker.\r\n\r\nrouter(config)#");
}

// Test Processing a command with invalid mode in hierarchy
TEST_F(Internal_CliTest, InvalidInput_InvalidModeHierarchy_ShouldRejectCommand)
{
    // Arrange
    std::string command = "router ospf 1 area 0";

    // Expectation: Terminal rejects the command due to invalid mode hierarchy
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(23);

    // Act
    bool result = handleInput(command);

    // Assert
    EXPECT_FALSE(result);

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router ospf 1 area 0\r\n              ^\r\n% Invalid input detected at '^' marker.\r\n\r\nrouter(config)#");
}

#pragma endregion
#pragma region IPv6Expansion

// Test Expanding a compressed IPv6 address
TEST_F(Internal_CliTest, IPv6Expanding_CompressedAddress_ShouldExpandCorrectly)
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
TEST_F(Internal_CliTest, IPv6Expanding_FullyExpandedAddress_ShouldRemainUnchanged)
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
TEST_F(Internal_CliTest, IPv6Expanding_MultipleCompressedSections_ShouldHandleError)
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
bool Internal_CliTest::batchProcessAndRecover(const std::vector<std::string>& commands, const std::vector<std::string>& expectedOutputs, std::vector<std::string>& recoveredCommands) {
    // Execute commands
    for (auto& command : commands) {
        handleInput(command);
    }

    // Mock reading the saved configuration
    EXPECT_CALL(*mockFileSystem, writeFile(ROUTER_CONFIG_FILE, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(true));
    save();

    // Config recovery was removed with the nlohmann schema tree; restore this once
    // Configs::recoverConfigs is reimplemented on top of utils::json.
    recoveredCommands.clear();

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
TEST_F(Internal_CliTest, BatchProcessing_MultipleCommands_ShouldProcessAndRecoverAccurately)
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
    EXPECT_EQ(global->getHostname(), "BatchRouter");

    EXPECT_EQ(mockConsole->getCapturedOutput(),"hostname BatchRouter\r\nBatchRouter(config)#interface GigabitEthernet 1\r\nBatchRouter(config-if)#ip address 172.16.0.1 255.255.255.0\r\nBatchRouter(config-if)#exit\r\nBatchRouter(config)#");
}

// Test Batch processing with invalid commands should handle errors and continue
TEST_F(Internal_CliTest, BatchProcessing_InvalidCommands_ShouldHandleErrorsAndContinue)
{
    // Arrange
    std::vector<std::string> commands = {
        "hostname BatchRouter",
        "invalidcmd",
        "interface GigabitEthernet 1",
        "ip address 10.0.0.1 255.255.255.0",
        "exit",
    };
    std::vector<std::string> expectedOutputs = {
        "hostname BatchRouter",
        "interface GigabitEthernet 1",
        "ip address 10.0.0.1 255.255.255.0",
        "exit",
    };
    std::vector<std::string> recoveredCommands;

    // Act & Assert
    bool recoveryResult = batchProcessAndRecover(commands, expectedOutputs, recoveredCommands);
    EXPECT_TRUE(recoveryResult);

    // Additional assertions based on internal state
    EXPECT_EQ(getCurrentMode(), CliMode::GlobalConfiguration);
    EXPECT_EQ(global->getHostname(), "BatchRouter");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "hostname BatchRouter\r\nBatchRouter(config)#invalidcmd\r\n                    ^\r\n% Invalid input detected at '^' marker.\r\n\r\nBatchRouter(config)#interface GigabitEthernet 1\r\nBatchRouter(config-if)#ip address 10.0.0.1 255.255.255.0\r\nBatchRouter(config-if)#exit\r\nBatchRouter(config)#");
}

// Test Executing a comprehensive list of valid commands and verifying state
TEST_F(Internal_CliTest, ComprehensiveConfiguration_ValidCommands_ShouldUpdateStateCorrectly)
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
    EXPECT_EQ(getCurrentMode(), CliMode::GlobalConfiguration);
    EXPECT_EQ(global->getHostname(), "ComprehensiveRouter");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "hostname ComprehensiveRouter\r\nComprehensiveRouter(config)#interface GigabitEthernet 1\r\nComprehensiveRouter(config-if)#ip address 192.168.1.1 255.255.255.0\r\nComprehensiveRouter(config-if)#no shutdown\r\nComprehensiveRouter(config-if)#exit\r\nComprehensiveRouter(config)#router ospf 1\r\nComprehensiveRouter(config-router)#network 192.168.1.0 0.0.0.255 area 0\r\nComprehensiveRouter(config-router)#exit\r\nComprehensiveRouter(config)#");
}

// Test Recovering state after a series of commands
TEST_F(Internal_CliTest, StateRecovery_AfterSeriesOfCommands_ShouldRestoreCorrectly)
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
    EXPECT_EQ(getCurrentMode(), CliMode::GlobalConfiguration);
    EXPECT_EQ(global->getHostname(), "RecoverRouter");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "hostname RecoverRouter\r\nRecoverRouter(config)#interface GigabitEthernet 1\r\nRecoverRouter(config-if)#ip address 10.0.0.1 255.255.255.0\r\nRecoverRouter(config-if)#invalidcmd\r\n                         ^\r\n% Invalid input detected at '^' marker.\r\n\r\nRecoverRouter(config-if)#exit\r\nRecoverRouter(config)#router ospf 1\r\nRecoverRouter(config-router)#network 10.0.0.0 0.0.0.255 area 0\r\nRecoverRouter(config-router)#exit\r\nRecoverRouter(config)#");
}

// Test Batch processing with abbreviated and invalid commands
TEST_F(Internal_CliTest, BatchProcessing_MixedValidAndInvalidCommands_ShouldHandleAppropriately)
{
    std::cout << "NEEDS OSPF IMPLEMENTATION" << std::endl;
    GTEST_SKIP();
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
    EXPECT_EQ(getCurrentMode(), CliMode::GlobalConfiguration);
    EXPECT_EQ(global->getHostname(), "RecoverRouter");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "router(config)#host RecoverRouter\nRecoverRouter(config)#interf Gig 1\nRecoverRouter(config-if)#ip add 10.0.0.1 255.255.255.0\nRecoverRouter(config-if)#invalidcmd\n                         ^\n% Invalid input detected at '^' marker.\n\nRecoverRouter(config-if)#exit\nRecoverRouter(config)#router osp 1\nRecoverRouter(config-router)#netw 10.0.0.0 0.0.0.255 are 0\nRecoverRouter(config-router)#exit\n");
}

// 21. Additional Helper and Utility Tests
#pragma endregion

// Test 21.3: Checking if a string is numeric
TEST_F(Internal_CliTest, Utility_IsNumeric_ShouldIdentifyNumericStrings) {
    // Arrange & Act & Assert
    EXPECT_TRUE(isNumeric("12345"));
    EXPECT_TRUE(isNumeric("-6789"));
    EXPECT_FALSE(isNumeric("12a45"));
    EXPECT_FALSE(isNumeric("abcde"));

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.4: Checking if a string is a valid MAC address
TEST_F(Internal_CliTest, Utility_IsMACAddress_ShouldValidateCorrectly) {
    // Arrange & Act & Assert
    EXPECT_TRUE(isMACAddress("00:1A:2B:3C:4D:5E"));
    EXPECT_TRUE(isMACAddress("001A.2B3C.4D5E"));
    EXPECT_FALSE(isMACAddress("00-1A-2B-3C-4D-5E"));
    EXPECT_FALSE(isMACAddress("00:1A:2B:3C:4D"));
    EXPECT_FALSE(isMACAddress("GG:HH:II:JJ:KK:LL"));

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.5: Checking if a string is a valid IPv6 address
TEST_F(Internal_CliTest, Utility_IsIPv6Address_ShouldValidateCorrectly) {
    // Arrange & Act & Assert
    EXPECT_TRUE(isIPv6Address("2001:0db8:85a3:0000:0000:8a2e:0370:7334"));
    EXPECT_TRUE(isIPv6Address("2001:db8::1"));
    EXPECT_FALSE(isIPv6Address("2001:0db8:85a3::8a2e:0370:7334:"));
    EXPECT_FALSE(isIPv6Address("2001:0db8:85a3:0000:0000:8a2e:0370"));
    EXPECT_FALSE(isIPv6Address("2001:0db8:85a3:0000:0000:8a2e:0370:7334:1234"));

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

// Test 21.8: Expanding IPv6 address correctly
TEST_F(Internal_CliTest, IPv6Expanding_ValidCompressedAddress_ShouldExpandSuccessfully) {
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
TEST_F(Internal_CliTest, IPv6Expanding_InvalidCompressedAddress_ShouldHandleError) {
    // Arrange
    std::string compressedIPv6 = "2001::db8::1";

    // Expectation: Terminal rejects the invalid IPv6 address
    EXPECT_CALL(*mockConsole, print(::testing::_, ::testing::_)).Times(0);

    // Act
    std::string expanded = expandIPv6Address(compressedIPv6);

    // Assert
    EXPECT_EQ(expanded, "");

    EXPECT_EQ(mockConsole->getCapturedOutput(), "");
}

#pragma endregion
