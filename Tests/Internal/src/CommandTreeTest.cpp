#include <gtest/gtest.h>
#include <cli/tree/CommandTree.h>
#include <cli/tree/nodes/ModeEntry.h>
#include <cli/tree/nodes/Command.h>
#include <utils/Json.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace cli::tree;

namespace
{
/**
 * A grammar directory that deletes itself.
 *
 * flattenDir reads the grammar off disk, so a case that states its grammar
 * inline needs it materialized somewhere first. Holding the directory in a
 * guard keeps that a detail of build() rather than something each test cleans
 * up, and keeps parallel runs off each other's files.
 */
class TempGrammarDir
{
public:
    TempGrammarDir()
    {
        static std::atomic<unsigned> counter{0};
        dir = std::filesystem::temp_directory_path()
            / ("cmdtree-test-" + std::to_string(::getpid())
               + "-" + std::to_string(counter++));
        std::filesystem::create_directories(dir);
    }

    ~TempGrammarDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    TempGrammarDir(const TempGrammarDir&) = delete;
    TempGrammarDir& operator=(const TempGrammarDir&) = delete;

    void write(const std::string& stem, const std::string& text) const
    {
        std::ofstream out(dir / (stem + ".json"));
        out << text;
    }

    std::string path() const { return dir.string(); }

private:
    std::filesystem::path dir;
};

/// The raw text of the value at `key`, found by matching brackets from the
/// key's colon. The tests hold their grammar as text, and re-emitting it from
/// a parsed DOM would need a serializer that does not exist, so the value is
/// copied out verbatim.
std::string rawValueOf(const std::string& json, size_t colon)
{
    size_t i = json.find_first_not_of(" \t\r\n", colon + 1);
    if (i == std::string::npos)
        throw std::runtime_error("grammar literal: key has no value");

    const size_t start = i;
    int depth = 0;
    bool inStr = false;

    for (; i < json.size(); ++i)
    {
        const char c = json[i];
        if (inStr)
        {
            if (c == '\\') ++i;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '[' || c == '{') ++depth;
        else if (c == ']' || c == '}')
        {
            if (--depth == 0) return json.substr(start, i - start + 1);
            if (depth < 0) break;
        }
    }
    throw std::runtime_error("grammar literal: unterminated value");
}

/**
 * Splits a one-object grammar literal into the per-mode files flattenDir reads.
 *
 * The literal maps prompt to command list, which is how the grammar used to be
 * written; the directory format puts one mode in each file under its own
 * "prompt" and "commands" keys. Variants stay in the value untouched, since
 * flattenDir unwraps that form itself.
 *
 * Anything malformed enough that the split cannot find modes is written out
 * whole, so the parser is still the thing that rejects it and the negative
 * cases keep testing the parser rather than this helper.
 */
void explode(const std::string& json, const TempGrammarDir& out)
{
    utils::json::JsonParser probe(json);
    utils::json::JsonNode dom = probe.parseDocument();

    if (dom.type != utils::json::JsonNode::OBJECT)
    {
        out.write("Grammar", json);
        return;
    }

    size_t nth = 0;
    size_t cursor = 0;

    for (const utils::json::JsonNode& child : dom.children)
    {
        // Locate this key in the source so its value can be lifted verbatim.
        const std::string quoted = '"' + child.name + '"';
        const size_t at = json.find(quoted, cursor);
        if (at == std::string::npos)
            throw std::runtime_error("grammar literal: lost key " + child.name);

        const size_t colon = json.find(':', at + quoted.size());
        if (colon == std::string::npos)
            throw std::runtime_error("grammar literal: key without colon");

        const std::string value = rawValueOf(json, colon);
        cursor = colon + value.size();

        // The literal names the table with the in-document key; flattenDir
        // finds it by filename instead, so it moves to the file it expects.
        if (child.name == std::string(KEY_VARIABLES))
        {
            out.write(std::string(VARIABLES_STEM), value);
            continue;
        }

        out.write("Mode" + std::to_string(nth++),
                  "{\n  \"" + std::string(KEY_PROMPT) + "\": \"" + child.name
                  + "\",\n  \"" + std::string(KEY_COMMANDS) + "\": " + value + "\n}");
    }
}

// Builds a tree from a JSON literal so each case states its own grammar.
CommandTree build(const std::string& json)
{
    TempGrammarDir dir;
    explode(json, dir);
    return CommandTree(parser::flattenDir(dir.path()));
}

// The flattened bytes for a grammar literal, for cases that read the buffer.
std::vector<std::byte> flattenLiteral(const std::string& json)
{
    TempGrammarDir dir;
    explode(json, dir);
    return parser::flattenDir(dir.path());
}
}

class Internal_CommandTreeTest : public ::testing::Test {};

// MODE ENTRIES

TEST_F(Internal_CommandTreeTest, PlainModeHoldsCommandsDirectly)
{
    CommandTree t = build(R"({
        "(config)#": [
            { "name": "hostname", "description": "Set hostname" },
            { "name": "interface", "description": "" }
        ]
    })");

    ASSERT_EQ(t.modeCount(), 1u);
    ModeEntry e = t.modeEntry(0);
    EXPECT_EQ(e.name(), "(config)#");
    EXPECT_EQ(e.subName(), "");
    EXPECT_FALSE(e.hasSubMode());
    EXPECT_EQ(e.size(), 2u);
}

TEST_F(Internal_CommandTreeTest, SubModesBecomeOneEntryEach)
{
    CommandTree t = build(R"({
        "(config-if)#": [{
            "ethernet": [ { "name": "shutdown", "description": "" } ],
            "Vlan":     [ { "name": "mtu", "description": "" }, { "name": "name", "description": "" } ]
        }]
    })");

    ASSERT_EQ(t.modeCount(), 2u);

    ModeEntry a = t.modeEntry(0);
    EXPECT_EQ(a.name(), "(config-if)#");
    EXPECT_EQ(a.subName(), "ethernet");
    EXPECT_TRUE(a.hasSubMode());
    EXPECT_EQ(a.size(), 1u);

    ModeEntry b = t.modeEntry(1);
    EXPECT_EQ(b.name(), "(config-if)#");
    EXPECT_EQ(b.subName(), "Vlan");
    EXPECT_EQ(b.size(), 2u);
}

// A submode name may contain the same characters as a mode name; nothing
// splits either string, so this must round trip intact.
TEST_F(Internal_CommandTreeTest, SubModeNameWithDashRoundTrips)
{
    CommandTree t = build(R"({
        "(config-if)#": [{ "Virtual-Template": [ { "name": "exit", "description": "" } ] }]
    })");

    ASSERT_EQ(t.modeCount(), 1u);
    EXPECT_EQ(t.modeEntry(0).name(), "(config-if)#");
    EXPECT_EQ(t.modeEntry(0).subName(), "Virtual-Template");
}

TEST_F(Internal_CommandTreeTest, VariablesKeyIsNotAMode)
{
    CommandTree t = build(R"({
        "(config)#": [ { "name": "hostname", "description": "" } ],
        "VARIABLES": { "WORD": [ "a", "b" ] }
    })");

    ASSERT_EQ(t.modeCount(), 1u);
    EXPECT_EQ(t.modeEntry(0).name(), "(config)#");
}

TEST_F(Internal_CommandTreeTest, NestedSubModesAreRejected)
{
    EXPECT_THROW(build(R"({
        "(config-router-af)#": [{ "eigrp": [{ "ipv4": [ { "name": "network", "description": "" } ] }] }]
    })"), std::runtime_error);
}

// LOOKUP

TEST_F(Internal_CommandTreeTest, FindModeMatchesModeAndSubMode)
{
    CommandTree t = build(R"({
        "(config)#":    [ { "name": "hostname", "description": "" } ],
        "(config-if)#": [{
            "ethernet": [ { "name": "shutdown", "description": "" } ],
            "Vlan":     [ { "name": "mtu", "description": "" } ]
        }]
    })");

    EXPECT_EQ(t.findMode("(config)#"), 0u);
    EXPECT_EQ(t.findMode("(config-if)#", "Vlan"), 2u);

    // A mode that routes to submodes has no entry under its bare name, and a
    // submode is not reachable under the wrong mode.
    EXPECT_EQ(t.findMode("(config-if)#"), CommandTree::NPOS);
    EXPECT_EQ(t.findMode("(config)#", "Vlan"), CommandTree::NPOS);
    EXPECT_EQ(t.findMode("nope"), CommandTree::NPOS);
}

// COMMANDS

TEST_F(Internal_CommandTreeTest, CommandsExposeNameAndDescription)
{
    CommandTree t = build(R"({
        "(config)#": [
            { "name": "hostname", "description": "Set hostname" },
            { "name": "bare", "description": "" }
        ]
    })");

    Command c = t.modeEntry(0).commands();
    ASSERT_EQ(c.size(), 2u);
    EXPECT_EQ(c.at(0).name(), "hostname");
    EXPECT_EQ(c.at(0).desc(), "Set hostname");

    // An absent description reads as empty rather than borrowing a neighbour's
    // bytes out of the shared blob.
    EXPECT_EQ(c.at(1).name(), "bare");
    EXPECT_EQ(c.at(1).desc(), "");
}

TEST_F(Internal_CommandTreeTest, SubcommandsNestToArbitraryDepth)
{
    CommandTree t = build(R"({
        "(config)#": [{
            "name": "ip", "description": "",
            "subcommands": [{
                "name": "route", "description": "",
                "subcommands": [ { "name": "A.B.C.D", "description": "Prefix" } ]
            }]
        }]
    })");

    Command root = t.modeEntry(0).commands();
    ASSERT_EQ(root.size(), 1u);
    Command ip = root.at(0);
    EXPECT_EQ(ip.name(), "ip");

    ASSERT_EQ(ip.size(), 1u);
    Command route = ip.at(0);
    EXPECT_EQ(route.name(), "route");

    ASSERT_EQ(route.size(), 1u);
    EXPECT_EQ(route.at(0).name(), "A.B.C.D");
    EXPECT_EQ(route.at(0).desc(), "Prefix");
    EXPECT_EQ(route.at(0).size(), 0u);
}

TEST_F(Internal_CommandTreeTest, FindLocatesSubcommandByName)
{
    CommandTree t = build(R"({
        "(config)#": [
            { "name": "hostname", "description": "" },
            { "name": "interface", "description": "" },
            { "name": "router", "description": "" }
        ]
    })");

    Command c = t.modeEntry(0).commands();
    EXPECT_EQ(c.find("interface"), 1u);
    EXPECT_EQ(c.find("router"), 2u);
    EXPECT_EQ(c.find("absent"), Command::NPOS);
}

// FLAGS

TEST_F(Internal_CommandTreeTest, PropertiesBecomeFlags)
{
    CommandTree t = build(R"({
        "(config)#": [
            { "name": "a", "description": "", "properties": [ "negate", "recursive" ] },
            { "name": "b", "description": "" }
        ]
    })");

    Command c = t.modeEntry(0).commands();
    EXPECT_TRUE(c.at(0).node().has(CommandNode::NEGATE));
    EXPECT_TRUE(c.at(0).node().has(CommandNode::RECURSIVE));
    EXPECT_FALSE(c.at(0).node().has(CommandNode::NEGATE_ALL));
    EXPECT_FALSE(c.at(1).node().has(CommandNode::NEGATE));
}

TEST_F(Internal_CommandTreeTest, UnknownPropertyIsRejected)
{
    EXPECT_THROW(build(R"({
        "(config)#": [ { "name": "a", "description": "", "properties": [ "not_a_property" ] } ]
    })"), std::runtime_error);
}

// MALFORMED INPUT

TEST_F(Internal_CommandTreeTest, DocumentRootMustBeAnObject)
{
    EXPECT_THROW(build(R"([ { "name": "hostname", "description": "" } ])"), std::runtime_error);
}

TEST_F(Internal_CommandTreeTest, CommandWithoutNameIsRejected)
{
    EXPECT_THROW(build(R"({
        "(config)#": [ { "description": "no name here" } ]
    })"), std::runtime_error);
}

TEST_F(Internal_CommandTreeTest, OutOfRangeAccessThrows)
{
    CommandTree t = build(R"({ "(config)#": [ { "name": "hostname", "description": "" } ] })");
    EXPECT_THROW(t.modeEntry(1), std::runtime_error);
    EXPECT_THROW(t.modeEntry(0).commands().at(1), std::runtime_error);
}

// ROUND TRIP

// The reader must work off the serialized bytes alone, since production loads
// the binary from disk rather than from the builder's vectors.
TEST_F(Internal_CommandTreeTest, SerializedBufferReadsBackIdentically)
{
    const std::string json = R"({
        "(config)#":    [ { "name": "hostname", "description": "Set hostname" } ],
        "(config-if)#": [{ "ethernet": [ { "name": "shutdown", "description": "" } ] }]
    })";

    std::vector<std::byte> bytes = flattenLiteral(json);

    CommandTree t{std::vector<std::byte>(bytes)};
    ASSERT_EQ(t.modeCount(), 2u);
    EXPECT_EQ(t.modeEntry(0).name(), "(config)#");
    EXPECT_EQ(t.modeEntry(0).commands().at(0).desc(), "Set hostname");
    EXPECT_EQ(t.modeEntry(1).subName(), "ethernet");
    EXPECT_EQ(t.modeEntry(1).commands().at(0).name(), "shutdown");
}

TEST_F(Internal_CommandTreeTest, TruncatedBufferIsRejected)
{
    CommandTree t = build(R"({ "(config)#": [ { "name": "hostname", "description": "" } ] })");

    std::vector<std::byte> bytes =
        flattenLiteral(R"({ "(config)#": [ { "name": "hostname", "description": "" } ] })");
    bytes.resize(bytes.size() - 4);

    EXPECT_THROW(CommandTree(std::move(bytes)), std::runtime_error);
}

// RUNTIME PORT NUMBERING

namespace
{
constexpr const char* PORT_GRAMMAR = R"({
    "(config)#": [
        { "name": "interface", "description": "", "subcommands": [
            { "name": "GigabitEthernet", "description": "Gigabit port", "subcommands": [
                { "name": "<0>", "description": "Interface number" }
            ]},
            { "name": "FastEthernet", "description": "", "subcommands": [
                { "name": "<0>", "description": "Interface number" }
            ]}
        ]}
    ]
})";

// The "<N>" child hanging off the named interface type.
Command portNode(const CommandTree& t, std::string_view ifaceType)
{
    Command iface = t.modeEntry(0).commands().at(0);
    return iface.at(iface.find(ifaceType)).at(0);
}
}

TEST_F(Internal_CommandTreeTest, PortPlaceholderExpandsToConfiguredCount)
{
    CommandTree t = build(PORT_GRAMMAR);
    EXPECT_EQ(portNode(t, "GigabitEthernet").name(), "<0>");

    t.applyPortCounts({{"GigabitEthernet", 10}});
    EXPECT_EQ(portNode(t, "GigabitEthernet").name(), "<0-9>");
}

TEST_F(Internal_CommandTreeTest, PortPlaceholderIsNumberedPerInterfaceType)
{
    CommandTree t = build(PORT_GRAMMAR);
    t.applyPortCounts({{"GigabitEthernet", 10}, {"FastEthernet", 4}});

    // Same placeholder text in the grammar, different hardware behind each.
    EXPECT_EQ(portNode(t, "GigabitEthernet").name(), "<0-9>");
    EXPECT_EQ(portNode(t, "FastEthernet").name(), "<0-3>");
}

TEST_F(Internal_CommandTreeTest, UnconfiguredInterfaceTypeKeepsItsPlaceholder)
{
    CommandTree t = build(PORT_GRAMMAR);
    t.applyPortCounts({{"GigabitEthernet", 10}});

    // A type the hardware does not have gets no range, so no port number can
    // match it -- which is the correct answer for a port that does not exist.
    EXPECT_EQ(portNode(t, "FastEthernet").name(), "<0>");
}

TEST_F(Internal_CommandTreeTest, PatchedNodeKeepsItsDescription)
{
    CommandTree t = build(PORT_GRAMMAR);
    t.applyPortCounts({{"GigabitEthernet", 10}});

    // The name moves to a separate buffer, and desc() reads at a fixed offset
    // past it, so the description has to travel with it.
    EXPECT_EQ(portNode(t, "GigabitEthernet").desc(), "Interface number");
}

TEST_F(Internal_CommandTreeTest, PatchingLeavesSiblingNamesIntact)
{
    CommandTree t = build(PORT_GRAMMAR);
    t.applyPortCounts({{"GigabitEthernet", 10}, {"FastEthernet", 4}});

    // Names are packed contiguously in the blob, so a widened name written in
    // place would run into whatever follows it.
    Command iface = t.modeEntry(0).commands().at(0);
    EXPECT_EQ(iface.name(), "interface");
    EXPECT_EQ(iface.at(0).name(), "GigabitEthernet");
    EXPECT_EQ(iface.at(0).desc(), "Gigabit port");
    EXPECT_EQ(iface.at(1).name(), "FastEthernet");
}

TEST_F(Internal_CommandTreeTest, ExistingRangeIsNotAPlaceholder)
{
    CommandTree t = build(R"({
        "(config)#": [
            { "name": "interface", "description": "", "subcommands": [
                { "name": "GigabitEthernet", "description": "", "subcommands": [
                    { "name": "<1-99>", "description": "Interface number" }
                ]}
            ]}
        ]
    })");

    t.applyPortCounts({{"GigabitEthernet", 10}});
    EXPECT_EQ(portNode(t, "GigabitEthernet").name(), "<1-99>");
}

TEST_F(Internal_CommandTreeTest, ApplyingPortCountsTwiceIsIdempotent)
{
    CommandTree t = build(PORT_GRAMMAR);
    t.applyPortCounts({{"GigabitEthernet", 10}});
    t.applyPortCounts({{"GigabitEthernet", 10}});

    // The second pass reads the mapping, not the patch, so the already-expanded
    // range is not mistaken for a placeholder and expanded again.
    EXPECT_EQ(portNode(t, "GigabitEthernet").name(), "<0-9>");
}
