#include <gtest/gtest.h>

#include <cli/execution/Executor.hpp>
#include <cli/modes/Context.hpp>
#include <cli/session/Token.hpp>
#include <cli/session/TreeNavigator.hpp>
#include <cli/tree/CommandTree.h>
#include <cli/tree/nodes/Command.h>
#include <cli/tree/nodes/ModeEntry.h>
#include <cli/tree/nodes/GrammarKeys.h>
#include <configs/RegistryTable.hpp>
#include <configs/registry/policy/PolicyHelpers.hpp>
#include <configs/registry/router/EigrpRegistry.h>
#include <configs/registry/router/OspfRegistry.h>
#include <utils/Json.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace cli;
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

CommandTree buildCommands(const std::string& commandsArray)
{
    TempGrammarDir dir;
    dir.write("Mode0",
        "{\n  \"prompt\": \"(config-router)#\",\n"
        "  \"commands\": " + commandsArray + "\n}");
    return CommandTree(parser::flattenDir(dir.path()));
}

constexpr const char* DISTRIBUTE_GRAMMAR = R"([
    { "name": "distribute-list", "description": "",
      "subcommands": [
        { "name": "WORD", "description": "",
          "config": "Eigrp::DISTRIBUTE_LIST_IN::filterList",
          "subcommands": [
            { "name": "acl", "description": "",
              "config": "Eigrp::DISTRIBUTE_LIST_IN::distribution",
              "enum": "DistributeListType::ACL",
              "subcommands": [
                { "name": "<cr>", "description": "" },
                { "name": "max-event-log-size", "description": "",
                  "subcommands": [
                    { "name": "<1-4294967295>", "description": "",
                      "config": "Eigrp::MAX_EVENT_LOG_SIZE",
                      "subcommands": [ { "name": "<cr>", "description": "" } ] }
                  ] }
              ] },
            { "name": "route-map", "description": "",
              "config": "Eigrp::DISTRIBUTE_LIST_IN::distribution",
              "enum": "DistributeListType::ROUTE_MAP",
              "subcommands": [ { "name": "<cr>", "description": "" } ] }
          ] }
      ] }
])";

class Line
{
public:
    void push(std::string_view word, Command node) { nodes.push_back(node); words.push_back(word); }

    void pushCr(Command cr) { push("<cr>", cr); }

    std::span<Token> tokens()
    {
        toks.clear();
        for (size_t i = 0; i < words.size(); ++i)
            toks.emplace_back(words[i], nodes[i]);
        return std::span<Token>(toks);
    }

private:
    std::vector<Command>          nodes;
    std::vector<std::string_view> words;
    std::vector<Token>            toks;
};

// Executor wants a TreeNavigator, which wants a CommandTree, and ContextBase
// wants a CliSession that nothing on this path dereferences. Bundled so each
// test states only the grammar and the line.
//
// Templated on the registry because the cases below write into different ones:
// the distribute-list grammar edits Eigrp, the area grammar edits Ospf.
template <typename Registry>
struct BasicFixture
{
    explicit BasicFixture(const std::string& grammar)
        : tree(buildCommands(grammar)),
          ctx(*static_cast<CliSession*>(nullptr), static_cast<void*>(&reg)),
          nav(tree, ctx),
          exec(nav, ctx)
    {}

    Command root() { return tree.modeEntry(0).commands(); }
    Command cmd(size_t i) { return tree.modeEntry(0).commands().at(i); }

    Registry             reg;
    CommandTree          tree;
    ContextBase          ctx;
    TreeNavigator        nav;
    execution::Executor  exec;
};

using Fixture     = BasicFixture<config::EigrpRegistry>;
using OspfFixture = BasicFixture<config::OspfRegistry>;

std::vector<config::policy::DistributeList> entriesOf(config::EigrpRegistry& reg)
{
    std::vector<config::policy::DistributeList> out = 
        reg.get<config::Eigrp::DISTRIBUTE_LIST_IN>().get();
    return out;
}

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
    ::utils::json::JsonParser probe(json);
    ::utils::json::JsonNode dom = probe.parseDocument();

    if (dom.type != ::utils::json::JsonNode::OBJECT)
    {
        out.write("Grammar", json);
        return;
    }

    size_t nth = 0;
    size_t cursor = 0;

    for (const ::utils::json::JsonNode& child : dom.children)
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

std::vector<std::byte> flattenCommands(const std::string& commandsArray)
{
    TempGrammarDir dir;
    dir.write("Mode0",
        "{\n  \"prompt\": \"(config-router)#\",\n"
        "  \"commands\": " + commandsArray + "\n}");
    return parser::flattenDir(dir.path());
}

// One mode plus a Variables file, so a case can name a variable and control
// what it expands to.
CommandTree buildWithVariables(const std::string& commandsArray,
                               const std::string& variablesObject)
{
    TempGrammarDir dir;
    dir.write("Variables", variablesObject);
    dir.write("Mode0",
        "{\n  \"prompt\": \"(config-router)#\",\n"
        "  \"commands\": " + commandsArray + "\n}");
    return CommandTree(parser::flattenDir(dir.path()));
}

// The nth command of the only mode. Returned by value: node() hands back a
// reference into the cursor, so a temporary cursor would leave it dangling.
Command commandAt(const CommandTree& t, size_t i)
{
    return t.modeEntry(0).commands().at(i);
}

constexpr uint16_t ospfAreaRegistryId()
{
    return config::registryIdV<config::OspfArea>;
}

// `area <n>` stages the key; `<cr>` resolves it. The words in between belong to
// other commands and write where they always did -- the registry the line was
// typed in -- because the rescope the resolver performs happens at the end of
// the line, behind them. That is the arrangement the flattener enforces: under a
// deferred container the expectation stays at the parent registry, so a child
// naming the sub-registry is a grammar that would never have written.
//
// `reference-bandwidth` is such a word: an Ospf field, sitting between the key
// and the resolver, and the run in front of it must not swallow the `<cr>`.
constexpr const char* AREA_GRAMMAR = R"([
    { "name": "area", "description": "",
      "config": "Ospf::AREA_CONFIGS", "deferred": "area",
      "subcommands": [
        { "name": "<0-4294967295>", "description": "",
          "config": "Ospf::AREA_CONFIGS", "deferred": "area",
          "subcommands": [
            { "name": "<cr>", "description": "", "resolver": "area" },
            { "name": "reference-bandwidth", "description": "",
              "subcommands": [
                { "name": "<1-4294967>", "description": "",
                  "config": "Ospf::REFERENCE_BANDWIDTH",
                  "subcommands": [
                    { "name": "<cr>", "description": "", "resolver": "area" }
                  ] }
              ] },
            { "name": "lls", "description": "",
              "config": "Ospf::LLS",
              "subcommands": [
                { "name": "<cr>", "description": "", "resolver": "area" }
              ] }
          ] }
      ] }
])";

config::OspfAreaRegistry& areaAt(config::OspfRegistry& reg, uint32_t id)
{
    return reg.get<config::Ospf::AREA_CONFIGS>().emplaceBack(id);
}

size_t areaCount(config::OspfRegistry& reg)
{
    return reg.get<config::Ospf::AREA_CONFIGS>().get().size();
}
}

class Internal_CliTreeTest : public ::testing::Test {};

// MODE ENTRIES

TEST_F(Internal_CliTreeTest, PlainModeHoldsCommandsDirectly)
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

TEST_F(Internal_CliTreeTest, SubModesBecomeOneEntryEach)
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
TEST_F(Internal_CliTreeTest, SubModeNameWithDashRoundTrips)
{
    CommandTree t = build(R"({
        "(config-if)#": [{ "Virtual-Template": [ { "name": "exit", "description": "" } ] }]
    })");

    ASSERT_EQ(t.modeCount(), 1u);
    EXPECT_EQ(t.modeEntry(0).name(), "(config-if)#");
    EXPECT_EQ(t.modeEntry(0).subName(), "Virtual-Template");
}

TEST_F(Internal_CliTreeTest, VariablesKeyIsNotAMode)
{
    CommandTree t = build(R"({
        "(config)#": [ { "name": "hostname", "description": "" } ],
        "VARIABLES": { "WORD": [ "a", "b" ] }
    })");

    ASSERT_EQ(t.modeCount(), 1u);
    EXPECT_EQ(t.modeEntry(0).name(), "(config)#");
}

TEST_F(Internal_CliTreeTest, NestedSubModesAreRejected)
{
    EXPECT_THROW(build(R"({
        "(config-router-af)#": [{ "eigrp": [{ "ipv4": [ { "name": "network", "description": "" } ] }] }]
    })"), std::runtime_error);
}

// LOOKUP

TEST_F(Internal_CliTreeTest, FindModeMatchesModeAndSubMode)
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

TEST_F(Internal_CliTreeTest, CommandsExposeNameAndDescription)
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

TEST_F(Internal_CliTreeTest, SubcommandsNestToArbitraryDepth)
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

TEST_F(Internal_CliTreeTest, FindLocatesSubcommandByName)
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

TEST_F(Internal_CliTreeTest, PropertiesBecomeFlags)
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

TEST_F(Internal_CliTreeTest, UnknownPropertyIsRejected)
{
    EXPECT_THROW(build(R"({
        "(config)#": [ { "name": "a", "description": "", "properties": [ "not_a_property" ] } ]
    })"), std::runtime_error);
}

// MALFORMED INPUT

TEST_F(Internal_CliTreeTest, DocumentRootMustBeAnObject)
{
    EXPECT_THROW(build(R"([ { "name": "hostname", "description": "" } ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, CommandWithoutNameIsRejected)
{
    EXPECT_THROW(build(R"({
        "(config)#": [ { "description": "no name here" } ]
    })"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, OutOfRangeAccessThrows)
{
    CommandTree t = build(R"({ "(config)#": [ { "name": "hostname", "description": "" } ] })");
    EXPECT_THROW(t.modeEntry(1), std::runtime_error);
    EXPECT_THROW(t.modeEntry(0).commands().at(1), std::runtime_error);
}

// ROUND TRIP

// The reader must work off the serialized bytes alone, since production loads
// the binary from disk rather than from the builder's vectors.
TEST_F(Internal_CliTreeTest, SerializedBufferReadsBackIdentically)
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

TEST_F(Internal_CliTreeTest, TruncatedBufferIsRejected)
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

TEST_F(Internal_CliTreeTest, PortPlaceholderExpandsToConfiguredCount)
{
    CommandTree t = build(PORT_GRAMMAR);
    EXPECT_EQ(portNode(t, "GigabitEthernet").name(), "<0>");

    t.applyPortCounts({{"GigabitEthernet", 10}});
    EXPECT_EQ(portNode(t, "GigabitEthernet").name(), "<0-9>");
}

TEST_F(Internal_CliTreeTest, PortPlaceholderIsNumberedPerInterfaceType)
{
    CommandTree t = build(PORT_GRAMMAR);
    t.applyPortCounts({{"GigabitEthernet", 10}, {"FastEthernet", 4}});

    // Same placeholder text in the grammar, different hardware behind each.
    EXPECT_EQ(portNode(t, "GigabitEthernet").name(), "<0-9>");
    EXPECT_EQ(portNode(t, "FastEthernet").name(), "<0-3>");
}

TEST_F(Internal_CliTreeTest, UnconfiguredInterfaceTypeKeepsItsPlaceholder)
{
    CommandTree t = build(PORT_GRAMMAR);
    t.applyPortCounts({{"GigabitEthernet", 10}});

    // A type the hardware does not have gets no range, so no port number can
    // match it -- which is the correct answer for a port that does not exist.
    EXPECT_EQ(portNode(t, "FastEthernet").name(), "<0>");
}

TEST_F(Internal_CliTreeTest, PatchedNodeKeepsItsDescription)
{
    CommandTree t = build(PORT_GRAMMAR);
    t.applyPortCounts({{"GigabitEthernet", 10}});

    // The name moves to a separate buffer, and desc() reads at a fixed offset
    // past it, so the description has to travel with it.
    EXPECT_EQ(portNode(t, "GigabitEthernet").desc(), "Interface number");
}

TEST_F(Internal_CliTreeTest, PatchingLeavesSiblingNamesIntact)
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

TEST_F(Internal_CliTreeTest, ExistingRangeIsNotAPlaceholder)
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

TEST_F(Internal_CliTreeTest, ApplyingPortCountsTwiceIsIdempotent)
{
    CommandTree t = build(PORT_GRAMMAR);
    t.applyPortCounts({{"GigabitEthernet", 10}});
    t.applyPortCounts({{"GigabitEthernet", 10}});

    // The second pass reads the mapping, not the patch, so the already-expanded
    // range is not mistaken for a placeholder and expanded again.
    EXPECT_EQ(portNode(t, "GigabitEthernet").name(), "<0-9>");
}


TEST_F(Internal_CliTreeTest, TheKeywordWritesItsEnumIntoTheEntry)
{
    Fixture f(DISTRIBUTE_GRAMMAR);

    Command dl   = f.cmd(0);
    Command word = dl.at(0);
    Command acl  = word.at(0);

    Line line;
    line.push("distribute-list", dl);
    line.push("FILTER", word);
    line.push("acl", acl);
    line.pushCr(acl.at(0));

    ASSERT_TRUE(f.exec.execute(line.tokens()));

    const auto entries = entriesOf(f.reg);
    ASSERT_EQ(entries.size(), 1u);

    EXPECT_EQ(std::get<0>(entries[0]), config::policy::DistributeListType::ACL);
    EXPECT_EQ(std::get<2>(entries[0]), "FILTER");
}

TEST_F(Internal_CliTreeTest, TheSiblingKeywordWritesTheOtherValue)
{
    Fixture f(DISTRIBUTE_GRAMMAR);

    Command dl   = f.cmd(0);
    Command word = dl.at(0);
    Command rm   = word.at(1);

    Line line;
    line.push("distribute-list", dl);
    line.push("FILTER", word);
    line.push("route-map", rm);
    line.pushCr(rm.at(0));

    ASSERT_TRUE(f.exec.execute(line.tokens()));

    const auto entries = entriesOf(f.reg);
    ASSERT_EQ(entries.size(), 1u);

    EXPECT_EQ(std::get<0>(entries[0]), config::policy::DistributeListType::ROUTE_MAP);
}

TEST_F(Internal_CliTreeTest, TheKeywordConsumesNoTokenOfItsOwn)
{
    Fixture f(DISTRIBUTE_GRAMMAR);

    Command dl    = f.cmd(0);
    Command word  = dl.at(0);
    Command acl   = word.at(0);
    Command mels  = acl.at(1);
    Command size  = mels.at(0);

    Line line;
    line.push("distribute-list", dl);
    line.push("FILTER", word);
    line.push("acl", acl);
    line.push("max-event-log-size", mels);
    line.push("900", size);
    line.pushCr(size.at(0));

    ASSERT_TRUE(f.exec.execute(line.tokens()));

    const auto entries = entriesOf(f.reg);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(std::get<0>(entries[0]), config::policy::DistributeListType::ACL);

    // The word behind the keyword reached the command it belonged to.
    EXPECT_EQ(f.reg.get<config::Eigrp::MAX_EVENT_LOG_SIZE>().load(), 900u);
}

TEST_F(Internal_CliTreeTest, DifferentEnumValuesAreDifferentEntries)
{
    Fixture f(DISTRIBUTE_GRAMMAR);

    Command dl   = f.cmd(0);
    Command word = dl.at(0);

    Line first;
    first.push("distribute-list", dl);
    first.push("FILTER", word);
    first.push("acl", word.at(0));
    first.pushCr(word.at(0).at(0));
    ASSERT_TRUE(f.exec.execute(first.tokens()));

    Line second;
    second.push("distribute-list", dl);
    second.push("FILTER", word);
    second.push("route-map", word.at(1));
    second.pushCr(word.at(1).at(0));
    ASSERT_TRUE(f.exec.execute(second.tokens()));

    const auto entries = entriesOf(f.reg);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_NE(std::get<0>(entries[0]), std::get<0>(entries[1]));
}

TEST_F(Internal_CliTreeTest, TheSameLineTwiceLeavesOneEntry)
{
    Fixture f(DISTRIBUTE_GRAMMAR);

    Command dl   = f.cmd(0);
    Command word = dl.at(0);
    Command acl  = word.at(0);

    for (int i = 0; i < 2; ++i)
    {
        Line line;
        line.push("distribute-list", dl);
        line.push("FILTER", word);
        line.push("acl", acl);
        line.pushCr(acl.at(0));
        ASSERT_TRUE(f.exec.execute(line.tokens()));
    }

    EXPECT_EQ(entriesOf(f.reg).size(), 1u);
}



// ===================================================================
// GRAMMAR BINDING
//
// What the flattener does with the three keys that bind a command to config:
// "config" for a field, "config" with a third part for one member of a tuple
// valued field, and "enum" for a field set by naming an enum member.
// ===================================================================


// PLAIN FIELD BINDING

TEST_F(Internal_CliTreeTest, ConfigKeyBindsRegistryAndField)
{
    CommandTree t = buildCommands(R"([
        { "name": "router-id", "description": "", "config": "OspfArea::AREA_TYPE" }
    ])");

    const Command c = commandAt(t, 0);
    const CommandNode& n = c.node();

    ASSERT_TRUE(n.hasConfig());
    EXPECT_EQ(n.fieldRegistryId(), ospfAreaRegistryId());
    EXPECT_EQ(n.enumIndex(), static_cast<uint16_t>(config::OspfArea::AREA_TYPE));

    // No enum or tuple key, so configExt stays unclaimed.
    EXPECT_EQ(n.configExt, CommandNode::CONFIG_EXT_NONE);
    EXPECT_FALSE(n.hasEnumChange());
    EXPECT_FALSE(n.hasTuple());
}

TEST_F(Internal_CliTreeTest, UnknownRegistryIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "NoSuchRegistry::FIELD" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, UnknownFieldIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea::NO_SUCH_FIELD" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, RegistryWithoutFieldIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea" }
    ])"), std::runtime_error);
}

// Two commands writing the same plain field would each claim its one slot.
TEST_F(Internal_CliTreeTest, FieldBoundTwiceIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "a", "description": "", "config": "OspfArea::AREA_TYPE" },
        { "name": "b", "description": "", "config": "OspfArea::AREA_TYPE" }
    ])"), std::runtime_error);
}

// ENUM BINDING

TEST_F(Internal_CliTreeTest, EnumKeyResolvesToTheMemberValue)
{
    CommandTree t = buildCommands(R"([
        { "name": "stub", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::STUB" }
    ])");

    const Command c = commandAt(t, 0);
    const CommandNode& n = c.node();

    ASSERT_TRUE(n.hasEnumChange());

    // The stored value is the member's own enum value, so it is written to the
    // field without a second lookup at execution.
    EXPECT_EQ(n.configExt, static_cast<uint8_t>(config::ospf::AreaType::STUB));
}

TEST_F(Internal_CliTreeTest, EachMemberGetsItsOwnValue)
{
    CommandTree t = buildCommands(R"([
        { "name": "normal", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::NORMAL" },
        { "name": "nssa", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::NSSA" }
    ])");

    EXPECT_EQ(commandAt(t, 0).node().configExt,
              static_cast<uint8_t>(config::ospf::AreaType::NORMAL));
    EXPECT_EQ(commandAt(t, 1).node().configExt,
              static_cast<uint8_t>(config::ospf::AreaType::NSSA));
}

// Several commands set the same enum field to different members, which is the
// normal shape -- so unlike a plain field they must not collide on its slot.
TEST_F(Internal_CliTreeTest, EnumMembersShareOneFieldWithoutColliding)
{
    EXPECT_NO_THROW(buildCommands(R"([
        { "name": "normal", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::NORMAL" },
        { "name": "stub", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::STUB" },
        { "name": "nssa", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::NSSA" }
    ])"));
}

TEST_F(Internal_CliTreeTest, EnumNamingTheWrongTypeIsRejected)
{
    // Resolves as a member name, but of an enum this field does not hold.
    EXPECT_THROW(buildCommands(R"([
        { "name": "stub", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "Duplex::STUB" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, EnumNamingAnAbsentMemberIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::NO_SUCH_MEMBER" }
    ])"), std::runtime_error);
}

// COUNT bounds the enum rather than being a value, so naming it would write an
// out of range value into the field.
TEST_F(Internal_CliTreeTest, EnumNamingCountIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::COUNT" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, EnumOnANonEnumFieldIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "OspfArea::RANGE", "enum": "AreaType::STUB" }
    ])"), std::runtime_error);
}

// The field is where the enum's type is checked, so without one there is
// nothing to check against.
TEST_F(Internal_CliTreeTest, EnumWithoutAConfigKeyIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "enum": "AreaType::STUB" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, EnumMustNameBothTypeAndMember)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "STUB" }
    ])"), std::runtime_error);
}

// TUPLE MEMBER BINDING

TEST_F(Internal_CliTreeTest, ThirdKeyPartResolvesATupleMember)
{
    CommandTree t = buildCommands(R"([
        { "name": "cost", "description": "", "config": "OspfArea::RANGE::cost" }
    ])");

    const Command c = commandAt(t, 0);
    const CommandNode& n = c.node();

    ASSERT_TRUE(n.hasConfig());
    ASSERT_TRUE(n.hasTuple());

    EXPECT_EQ(n.enumIndex(), static_cast<uint16_t>(config::OspfArea::RANGE));

    // The stored index is the std::get position, which is what the executor
    // stages the value under.
    EXPECT_EQ(n.configExt, 2u);
}

TEST_F(Internal_CliTreeTest, TupleMembersResolveToTheirOwnPositions)
{
    CommandTree t = buildCommands(R"([
        { "name": "prefix", "description": "", "config": "OspfArea::RANGE::prefix" },
        { "name": "advertise", "description": "", "config": "OspfArea::RANGE::advertise" },
        { "name": "cost", "description": "", "config": "OspfArea::RANGE::cost" }
    ])");

    EXPECT_EQ(commandAt(t, 0).node().configExt, 0u);
    EXPECT_EQ(commandAt(t, 1).node().configExt, 1u);
    EXPECT_EQ(commandAt(t, 2).node().configExt, 2u);

    // All three name the same field; only the member differs.
    EXPECT_EQ(commandAt(t, 0).node().configId, commandAt(t, 2).node().configId);
}

// Members of one tuple are separate commands writing one field, so unlike a
// plain field they must not collide on its slot.
TEST_F(Internal_CliTreeTest, TupleMembersShareOneFieldWithoutColliding)
{
    EXPECT_NO_THROW(buildCommands(R"([
        { "name": "prefix", "description": "", "config": "OspfArea::RANGE::prefix" },
        { "name": "cost", "description": "", "config": "OspfArea::RANGE::cost" }
    ])"));
}

TEST_F(Internal_CliTreeTest, UnknownTupleMemberIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea::RANGE::nosuchmember" }
    ])"), std::runtime_error);
}

// A field with no TUPLE_SCHEMA_FOR names no members at all, so a member on one
// is a grammar mistake rather than a lookup miss.
TEST_F(Internal_CliTreeTest, TupleMemberOnANonTupleFieldIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea::AREA_TYPE::cost" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, FourPartConfigKeyIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea::RANGE::cost::extra" }
    ])"), std::runtime_error);
}

// CONFIGEXT IS SHARED

// A mode, an enum and a tuple member all store into configExt, so a command may
// be only one of the three. Left unchecked the last key parsed would win and
// the other binding would read back as something it is not.
//
// The one pairing that is allowed is an enum on a tuple member whose own type
// is that enum, which the section below covers: there configExt is split rather
// than claimed twice.
TEST_F(Internal_CliTreeTest, EnumAndNonEnumTupleMemberTogetherAreRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "OspfArea::RANGE::cost", "enum": "AreaType::STUB" }
    ])"), std::runtime_error);
}

// ENUM VALUED TUPLE MEMBERS

// A keyword naming an enum member of a tuple says two things at once -- which
// member of the tuple, and what to set it to -- so configExt carries both, four
// bits each. The member half has to read back the same as it would on a member
// carrying no enum, since that is what the executor stages under.
TEST_F(Internal_CliTreeTest, AnEnumOnATupleMemberPacksBothIndexes)
{
    CommandTree t = buildCommands(R"([
        { "name": "acl", "description": "",
          "config": "Eigrp::DISTRIBUTE_LIST_IN::distribution",
          "enum": "DistributeListType::ACL" }
    ])");

    const Command c = commandAt(t, 0);
    const CommandNode& n = c.node();

    ASSERT_TRUE(n.hasTuple());
    ASSERT_TRUE(n.hasTupleEnum());

    EXPECT_EQ(n.enumIndex(), static_cast<uint16_t>(config::Eigrp::DISTRIBUTE_LIST_IN));

    // distribution is the first member of DistributeList, ACL the first
    // member of DistributeListType.
    EXPECT_EQ(n.tupleMember(), 0u);
    EXPECT_EQ(n.tupleEnumIndex(),
              static_cast<uint8_t>(config::policy::DistributeListType::ACL));
}

// The enum half is what tells two keywords on one member apart, so a value past
// the first has to survive the packing rather than reading back as zero.
TEST_F(Internal_CliTreeTest, SiblingsOnOneMemberDifferOnlyInTheEnum)
{
    CommandTree t = buildCommands(R"([
        { "name": "acl", "description": "",
          "config": "Eigrp::DISTRIBUTE_LIST_IN::distribution",
          "enum": "DistributeListType::ACL" },
        { "name": "route-map", "description": "",
          "config": "Eigrp::DISTRIBUTE_LIST_IN::distribution",
          "enum": "DistributeListType::ROUTE_MAP" }
    ])");

    const CommandNode& acl = commandAt(t, 0).node();
    const CommandNode& rm  = commandAt(t, 1).node();

    EXPECT_EQ(acl.configId, rm.configId);
    EXPECT_EQ(acl.tupleMember(), rm.tupleMember());

    EXPECT_EQ(rm.tupleEnumIndex(),
              static_cast<uint8_t>(config::policy::DistributeListType::ROUTE_MAP));
    EXPECT_NE(acl.tupleEnumIndex(), rm.tupleEnumIndex());
}

// The member's own type is the authority, not the field's: the field stores a
// whole tuple and is never an enum itself, so resolving against it would either
// miss or match the wrong type.
TEST_F(Internal_CliTreeTest, TheEnumTypeIsCheckedAgainstTheMemberNotTheField)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "Eigrp::DISTRIBUTE_LIST_IN::distribution",
          "enum": "AreaType::STUB" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, AnUnknownMemberOfATupleMemberEnumIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "Eigrp::DISTRIBUTE_LIST_IN::distribution",
          "enum": "DistributeListType::NO_SUCH_MEMBER" }
    ])"), std::runtime_error);
}

// Packed, the two indexes have four bits each, and both halves are checked
// rather than truncated -- a silently wrapped index would write a plausible
// wrong value with nothing to trace it back to.
TEST_F(Internal_CliTreeTest, BothPackedHalvesRoundTrip)
{
    for (uint16_t member = 0; member <= CommandNode::TUPLE_ENUM_MAX; ++member)
    {
        for (uint16_t idx = 0; idx <= CommandNode::TUPLE_ENUM_MAX; ++idx)
        {
            CommandNode n{};
            n.flags |= CommandNode::TUPLE_ENUM;
            n.configExt = CommandNode::packTupleEnum(member, idx);

            EXPECT_EQ(n.tupleMember(), member);
            EXPECT_EQ(n.tupleEnumIndex(), idx);
        }
    }
}

// An enum valued member is a tuple member, not an enum field: hasEnumChange
// reads configExt whole, and a member that also answered to it would hand back
// the two halves mashed together as if they were one index.
TEST_F(Internal_CliTreeTest, AnEnumValuedMemberIsNotAnEnumFieldChange)
{
    CommandTree t = buildCommands(R"([
        { "name": "acl", "description": "",
          "config": "Eigrp::DISTRIBUTE_LIST_IN::distribution",
          "enum": "DistributeListType::ACL" }
    ])");

    const CommandNode& n = commandAt(t, 0).node();

    EXPECT_TRUE(n.hasTuple());
    EXPECT_FALSE(n.hasEnumChange());
    EXPECT_FALSE(n.hasModeChange());
}

// The split lives in the packed node, so like every other binding it has to
// survive serialization rather than only being right in the builder's memory.
TEST_F(Internal_CliTreeTest, TheEnumMemberSplitSurvivesSerialization)
{
    CommandTree t(flattenCommands(R"([
        { "name": "route-map", "description": "",
          "config": "Eigrp::DISTRIBUTE_LIST_IN::distribution",
          "enum": "DistributeListType::ROUTE_MAP" }
    ])"));

    const CommandNode& n = commandAt(t, 0).node();

    ASSERT_TRUE(n.hasTupleEnum());
    EXPECT_EQ(n.tupleMember(), 0u);
    EXPECT_EQ(n.tupleEnumIndex(),
              static_cast<uint8_t>(config::policy::DistributeListType::ROUTE_MAP));
}

// ROUND TRIP

// The bindings live in the packed node, so they have to survive serialization
// rather than only being right in the builder's own memory.
TEST_F(Internal_CliTreeTest, BindingsSurviveSerialization)
{
    const std::string grammar = R"([
        { "name": "stub", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::STUB" },
        { "name": "cost", "description": "", "config": "OspfArea::RANGE::cost" }
    ])";

    CommandTree t(flattenCommands(grammar));

    const Command ec = commandAt(t, 0);
    const CommandNode& en = ec.node();
    EXPECT_TRUE(en.hasEnumChange());
    EXPECT_EQ(en.configExt, static_cast<uint8_t>(config::ospf::AreaType::STUB));

    const Command tc = commandAt(t, 1);
    const CommandNode& tup = tc.node();
    EXPECT_TRUE(tup.hasTuple());
    EXPECT_EQ(tup.configExt, 2u);
    EXPECT_EQ(tup.enumIndex(), static_cast<uint16_t>(config::OspfArea::RANGE));
}

// THE LOOKUP TABLES THE ABOVE RESOLVE THROUGH

// Both are constant expressions, and are asserted as such: a runtime EXPECT
// would pass on a table the flattener could not actually have used.
TEST_F(Internal_CliTreeTest, RuntimeTupleLookupMatchesTheTypedOne)
{
    constexpr uint16_t reg = config::registryIdV<config::OspfArea>;
    constexpr uint16_t fld = static_cast<uint16_t>(config::OspfArea::RANGE);

    static_assert(config::findTupleMemberAt(config::RegistryEntries{}, reg, fld,
                      config::tokenHash("cost")) == 2);
    static_assert(config::findTupleMemberAt(config::RegistryEntries{}, reg, fld,
                      config::tokenHash("nope")) == config::TUPLE_NOT_FOUND);
    SUCCEED();
}

TEST_F(Internal_CliTreeTest, RuntimeEnumLookupChecksTheFieldsType)
{
    constexpr uint16_t reg = config::registryIdV<config::OspfArea>;
    constexpr uint16_t fld = static_cast<uint16_t>(config::OspfArea::AREA_TYPE);

    constexpr config::EnumResolution good = config::resolveEnumAt(
        config::RegistryEntries{}, reg, fld, config::TUPLE_NOT_FOUND,
        config::tokenHash("AreaType"), config::tokenHash("STUB"));

    static_assert(good.fieldIsEnum);
    static_assert(good.typeMatched);
    static_assert(good.index == static_cast<uint16_t>(config::ospf::AreaType::STUB));

    // Right member name, wrong enum: caught by the type half rather than
    // resolving against whatever table happened to hold a "STUB".
    constexpr config::EnumResolution wrongType = config::resolveEnumAt(
        config::RegistryEntries{}, reg, fld, config::TUPLE_NOT_FOUND,
        config::tokenHash("Duplex"), config::tokenHash("STUB"));

    static_assert(wrongType.fieldIsEnum);
    static_assert(!wrongType.typeMatched);
    static_assert(wrongType.index == config::ENUM_NOT_FOUND);

    SUCCEED();
}

TEST_F(Internal_CliTreeTest, NonEnumFieldReportsItselfAsSuch)
{
    constexpr uint16_t reg = config::registryIdV<config::OspfArea>;
    constexpr uint16_t fld = static_cast<uint16_t>(config::OspfArea::RANGE);

    constexpr config::EnumResolution res = config::resolveEnumAt(
        config::RegistryEntries{}, reg, fld, config::TUPLE_NOT_FOUND,
        config::tokenHash("AreaType"), config::tokenHash("STUB"));

    static_assert(!res.fieldIsEnum);
    SUCCEED();
}

// VARIABLE ARGUMENTS
//
// A variable is one definition named from many places -- the shipped grammar
// expands "interface" at 138 of them -- so a binding cannot live on the
// definition. The call site passes it in and the definition says where it
// lands. Without this the site's own keys are simply dropped, which is what
// left every expanded interface command unbound.

TEST_F(Internal_CliTreeTest, ArgumentReachesTheVariablesLeaf)
{
    CommandTree t = buildWithVariables(
        R"([
            { "name": "area", "description": "", "subcommands": [
                { "name": "<kinds>", "description": "",
                  "args": { "config": "OspfArea::AREA_TYPE" } }
            ] }
        ])",
        R"({ "kinds": [ { "name": "stub", "description": "",
                          "config": "$config" } ] })");

    const Command leaf = commandAt(t, 0).at(0);

    EXPECT_EQ(leaf.name(), "stub");
    ASSERT_TRUE(leaf.node().hasConfig());
    EXPECT_EQ(leaf.node().fieldRegistryId(), ospfAreaRegistryId());
    EXPECT_EQ(leaf.node().enumIndex(),
              static_cast<uint16_t>(config::OspfArea::AREA_TYPE));
}

TEST_F(Internal_CliTreeTest, EveryExpandedSiblingGetsTheArgument)
{
    CommandTree t = buildWithVariables(
        R"([
            { "name": "area", "description": "", "subcommands": [
                { "name": "<kinds>", "description": "",
                  "args": { "config": "OspfArea::AREA_TYPE" } }
            ] }
        ])",
        R"({ "kinds": [
            { "name": "stub",   "description": "", "config": "$config" },
            { "name": "nssa",   "description": "", "config": "$config" },
            { "name": "normal", "description": "", "config": "$config" } ] })");

    const Command area = commandAt(t, 0);
    ASSERT_EQ(area.size(), 3u);

    // All three bind the one field, which is the shape the exclusivity check
    // has to tolerate: they are one field's worth of keyword, not three fields.
    for (size_t i = 0; i < area.size(); ++i)
        EXPECT_EQ(area.at(i).node().enumIndex(),
                  static_cast<uint16_t>(config::OspfArea::AREA_TYPE))
            << "sibling " << i;
}

TEST_F(Internal_CliTreeTest, UnpassedArgumentLeavesTheNodeUnbound)
{
    // The same definition reached from a site that passes nothing. It cannot be
    // an error: one call site wanting a binding does not make the other 137
    // malformed.
    CommandTree t = buildWithVariables(
        R"([
            { "name": "area", "description": "", "subcommands": [
                { "name": "<kinds>", "description": "" }
            ] }
        ])",
        R"({ "kinds": [ { "name": "stub", "description": "",
                          "config": "$config" } ] })");

    EXPECT_FALSE(commandAt(t, 0).at(0).node().hasConfig());
}

TEST_F(Internal_CliTreeTest, ArgumentsForwardThroughNestedVariables)
{
    // <outer> expands to a node that is itself <inner>, so the argument has to
    // survive a hop it is not consumed on.
    CommandTree t = buildWithVariables(
        R"([
            { "name": "area", "description": "", "subcommands": [
                { "name": "<outer>", "description": "",
                  "args": { "config": "OspfArea::AREA_TYPE" } }
            ] }
        ])",
        R"({
            "outer": [ { "name": "<inner>", "description": "",
                         "args": { "config": "$config" } } ],
            "inner": [ { "name": "stub", "description": "",
                         "config": "$config" } ]
        })");

    const Command leaf = commandAt(t, 0).at(0);

    EXPECT_EQ(leaf.name(), "stub");
    ASSERT_TRUE(leaf.node().hasConfig());
    EXPECT_EQ(leaf.node().enumIndex(),
              static_cast<uint16_t>(config::OspfArea::AREA_TYPE));
}

TEST_F(Internal_CliTreeTest, ArgumentCarriesAModeAndEnumToo)
{
    // config/enum/mode all read through the same substitution, so a variable
    // can be handed whichever of them the site means.
    CommandTree t = buildWithVariables(
        R"([
            { "name": "area", "description": "", "subcommands": [
                { "name": "<kinds>", "description": "",
                  "args": { "config": "OspfArea::AREA_TYPE",
                            "kind": "AreaType::STUB" } }
            ] }
        ])",
        R"({ "kinds": [ { "name": "stub", "description": "",
                          "config": "$config", "enum": "$kind" } ] })");

    const CommandNode& n = commandAt(t, 0).at(0).node();

    ASSERT_TRUE(n.hasEnumChange());
    EXPECT_EQ(n.configExt, static_cast<uint8_t>(config::ospf::AreaType::STUB));
}

TEST_F(Internal_CliTreeTest, NonObjectArgsIsRejected)
{
    EXPECT_THROW(buildWithVariables(
        R"([ { "name": "<kinds>", "description": "", "args": "nope" } ])",
        R"({ "kinds": [ { "name": "stub", "description": "" } ] })"),
        std::runtime_error);
}

TEST_F(Internal_CliTreeTest, NonStringArgumentValueIsRejected)
{
    EXPECT_THROW(buildWithVariables(
        R"([ { "name": "<kinds>", "description": "", "args": { "config": 7 } } ])",
        R"({ "kinds": [ { "name": "stub", "description": "" } ] })"),
        std::runtime_error);
}

TEST_F(Internal_CliTreeTest, DuplicateArgumentNameIsRejected)
{
    // Two values under one name: whichever won would be arbitrary, so neither
    // does.
    EXPECT_THROW(buildWithVariables(
        R"([ { "name": "<kinds>", "description": "",
               "args": { "config": "OspfArea::AREA_TYPE",
                         "config": "OspfArea::AREA_TYPE" } } ])",
        R"({ "kinds": [ { "name": "stub", "description": "" } ] })"),
        std::runtime_error);
}

// MODE EXIT
//
// `exit` and `end` leave a mode rather than enter one, so unlike a mode change
// they bind no field: where they land is whatever the navigation stack held.

// Both spellings are exits, and neither has to say so: the name carries it, so
// the 23 exit commands in the shipped grammar need no property of their own.
TEST_F(Internal_CliTreeTest, ExitAndEndAreModeExitsByName)
{
    CommandTree t = buildCommands(R"([
        { "name": "exit", "description": "" },
        { "name": "end",  "description": "" }
    ])");

    EXPECT_TRUE(commandAt(t, 0).node().hasModeExit());
    EXPECT_TRUE(commandAt(t, 1).node().hasModeExit());
}

// How far an exit unwinds is decided when it runs, off the navigation stack,
// rather than stored per command -- so nothing here distinguishes the two.
TEST_F(Internal_CliTreeTest, AnOrdinaryCommandIsNotAModeExit)
{
    CommandTree t = buildCommands(R"([
        { "name": "exited", "description": "" }
    ])");

    EXPECT_FALSE(commandAt(t, 0).node().hasModeExit());
}

// The flag alone is enough, where hasModeChange() also wants configExt. An exit
// names no mode, so requiring one would make every exit read as not-an-exit.
TEST_F(Internal_CliTreeTest, ModeExitNeedsNoBoundField)
{
    CommandTree t = buildCommands(R"([
        { "name": "exit", "description": "", "properties": ["mode_exit"] }
    ])");

    const CommandNode& n = commandAt(t, 0).node();

    EXPECT_TRUE(n.hasModeExit());
    EXPECT_FALSE(n.hasConfig());
    EXPECT_EQ(n.configExt, CommandNode::CONFIG_EXT_NONE);
}

TEST_F(Internal_CliTreeTest, ExitingAndEnteringAModeIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "properties": ["mode_exit"],
          "config": "OspfArea::AREA_TYPE", "mode": "(config-router)#" }
    ])"), std::runtime_error);
}

// ENUM BITMAP FIELDS

// A bitmap field stores flags, so the enum key names a bit rather than a value.
// The member resolves against the enum the bitmap is indexed by, which is not
// the field's own type -- that is the raw storage integer.
TEST_F(Internal_CliTreeTest, EnumOnABitMapFieldResolvesTheMember)
{
    CommandTree t = buildCommands(R"([
        { "name": "connected", "description": "",
          "config": "Eigrp::STUB", "enum": "Stub::CONNECTED" }
    ])");

    const CommandNode& n = commandAt(t, 0).node();

    ASSERT_TRUE(n.hasEnumChange());
    EXPECT_TRUE(n.hasEnumBitMap());
    EXPECT_EQ(n.configExt, static_cast<uint8_t>(config::eigrp::Stub::CONNECTED));
}

// The flag is what tells the executor to accumulate rather than replace, so a
// plain enum field must not carry it.
TEST_F(Internal_CliTreeTest, EnumOnAValueFieldIsNotMarkedAsABitMap)
{
    CommandTree t = buildCommands(R"([
        { "name": "stub", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::STUB" }
    ])");

    const CommandNode& n = commandAt(t, 0).node();

    ASSERT_TRUE(n.hasEnumChange());
    EXPECT_FALSE(n.hasEnumBitMap());
}

// The whole point of a bitmap: several commands name members of the one field,
// and the line sets all of them.
TEST_F(Internal_CliTreeTest, BitMapMembersShareOneFieldWithoutColliding)
{
    CommandTree t = buildCommands(R"([
        { "name": "connected", "description": "",
          "config": "Eigrp::STUB", "enum": "Stub::CONNECTED" },
        { "name": "summary", "description": "",
          "config": "Eigrp::STUB", "enum": "Stub::SUMMARY" },
        { "name": "static", "description": "",
          "config": "Eigrp::STUB", "enum": "Stub::STATIC" }
    ])");

    EXPECT_EQ(commandAt(t, 0).node().configExt,
              static_cast<uint8_t>(config::eigrp::Stub::CONNECTED));
    EXPECT_EQ(commandAt(t, 1).node().configExt,
              static_cast<uint8_t>(config::eigrp::Stub::SUMMARY));
    EXPECT_EQ(commandAt(t, 2).node().configExt,
              static_cast<uint8_t>(config::eigrp::Stub::STATIC));
}

TEST_F(Internal_CliTreeTest, BitMapEnumNamingAnAbsentMemberIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "Eigrp::STUB", "enum": "Stub::NO_SUCH_FLAG" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, BitMapEnumNamingTheWrongTypeIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "Eigrp::STUB", "enum": "AreaType::STUB" }
    ])"), std::runtime_error);
}

// MID-COMMAND REGISTRY CHANGE

// Binding a container is the whole declaration -- the field names a scope rather
// than a value, so there is nothing else the node could have meant.
TEST_F(Internal_CliTreeTest, BindingAContainerRescopesImplicitly)
{
    CommandTree t = buildCommands(R"([
        { "name": "area", "description": "", "config": "Ospf::AREA_CONFIGS" }
    ])");

    const CommandNode& n = commandAt(t, 0).node();

    EXPECT_TRUE(n.hasRegistryChange());
    ASSERT_TRUE(n.hasConfig());
    EXPECT_EQ(n.enumIndex(), static_cast<uint16_t>(config::Ospf::AREA_CONFIGS));

    // A rescope names no mode, so unlike a mode change it leaves configExt free.
    EXPECT_EQ(n.configExt, CommandNode::CONFIG_EXT_NONE);
    EXPECT_FALSE(n.hasModeChange());
}

// A value field is written, not entered, so it is left alone.
TEST_F(Internal_CliTreeTest, BindingAValueFieldDoesNotRescope)
{
    CommandTree t = buildCommands(R"([
        { "name": "x", "description": "", "config": "Ospf::REFERENCE_BANDWIDTH" }
    ])");

    EXPECT_FALSE(commandAt(t, 0).node().hasRegistryChange());
}

// A mode change moves the same pointer but means it to persist, so it wins.
TEST_F(Internal_CliTreeTest, AModeChangeOnAContainerIsNotARescope)
{
    CommandTree t = buildCommands(R"([
        { "name": "x", "description": "", "config": "Ospf::AREA_CONFIGS",
          "mode": "(config-std-nacl)#" }
    ])");

    const CommandNode& n = commandAt(t, 0).node();
    EXPECT_TRUE(n.hasModeChange());
    EXPECT_FALSE(n.hasRegistryChange());
}

// A rescope resolves its field to a registry rather than writing it, so a
// member named alongside would be silently dropped.
TEST_F(Internal_CliTreeTest, AContainerThatAlsoSetsAnEnumIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "Ospf::AREA_CONFIGS",
          "enum": "AreaType::STUB" }
    ])"), std::runtime_error);
}

// Everything below a rescope is written into the registry it moved to; a field
// of any other one would resolve against a scope that is not there.
TEST_F(Internal_CliTreeTest, ADescendantOfTheRescopedRegistryIsAccepted)
{
    CommandTree t = buildCommands(R"([
        { "name": "area", "description": "", "config": "Ospf::AREA_CONFIGS",
          "subcommands": [
            { "name": "stub", "description": "", "config": "OspfArea::AREA_TYPE",
              "enum": "AreaType::STUB" }
          ] }
    ])");

    const Command area = commandAt(t, 0);
    EXPECT_TRUE(area.node().hasRegistryChange());

    ASSERT_EQ(area.size(), 1u);
    EXPECT_EQ(area.at(0).node().fieldRegistryId(),
              config::registryIdV<config::OspfArea>);
}

TEST_F(Internal_CliTreeTest, ADescendantOfAnotherRegistryIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "area", "description": "", "config": "Ospf::AREA_CONFIGS",
          "subcommands": [
            { "name": "priority", "description": "", "config": "Ospf::REFERENCE_BANDWIDTH" }
          ] }
    ])"), std::runtime_error);
}

// A descendant that writes nothing has no registry to be wrong about.
TEST_F(Internal_CliTreeTest, ADescendantWithNoConfigIsAccepted)
{
    CommandTree t = buildCommands(R"([
        { "name": "area", "description": "", "config": "Ospf::AREA_CONFIGS",
          "subcommands": [
            { "name": "filler", "description": "" }
          ] }
    ])");

    EXPECT_TRUE(commandAt(t, 0).node().hasRegistryChange());
}

// The nearer rescope is the one in force, so a second one rebases what its own
// subtree has to name rather than being measured against the first.
TEST_F(Internal_CliTreeTest, ANestedRescopeRebasesTheExpectation)
{
    CommandTree t = buildCommands(R"([
        { "name": "address-family", "description": "",
          "config": "Bgp::ADDRESS_FAMILIES",
          "subcommands": [
            { "name": "base", "description": "",
              "config": "BgpAddressFamily::AF_BASE",
              "subcommands": [
                { "name": "x", "description": "",
                  "config": "BgpAfBase::DEFAULT_ORIGINATE" }
              ] }
          ] }
    ])");

    // An owned list and a reference container are both containers, so the chain
    // rescopes twice and the leaf lands in the innermost registry.
    const Command af = commandAt(t, 0);
    ASSERT_EQ(af.size(), 1u);
    EXPECT_TRUE(af.node().hasRegistryChange());

    const Command base = af.at(0);
    ASSERT_EQ(base.size(), 1u);
    EXPECT_TRUE(base.node().hasRegistryChange());

    EXPECT_EQ(base.at(0).node().fieldRegistryId(),
              config::registryIdV<config::BgpAfBase>);
}

// A rescope names a field of the registry it is leaving, so it is checked
// against the outer expectation before it rebases anything.
TEST_F(Internal_CliTreeTest, ANestedRescopeOfAForeignRegistryIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "area", "description": "", "config": "Ospf::AREA_CONFIGS",
          "subcommands": [
            { "name": "again", "description": "", "config": "Ospf::AREA_CONFIGS" }
          ] }
    ])"), std::runtime_error);
}

// Both flags ride in the same uint16 as the rest, so they have to survive the
// round trip the way every other binding does.
TEST_F(Internal_CliTreeTest, NewFlagsSurviveSerialization)
{
    CommandTree t(flattenCommands(R"([
        { "name": "connected", "description": "",
          "config": "Eigrp::STUB", "enum": "Stub::CONNECTED" },
        { "name": "area", "description": "", "config": "Ospf::AREA_CONFIGS" }
    ])"));

    const CommandNode& flag = commandAt(t, 0).node();
    EXPECT_TRUE(flag.hasEnumBitMap());
    EXPECT_EQ(flag.configExt, static_cast<uint8_t>(config::eigrp::Stub::CONNECTED));

    EXPECT_TRUE(commandAt(t, 1).node().hasRegistryChange());
}

// DEFERRED VALUES AND RESOLVERS

// A deferred command still names its field -- what the key changes is when the
// write lands, not where.
TEST_F(Internal_CliTreeTest, DeferredKeepsItsFieldAndCarriesTheKey)
{
    CommandTree t = buildCommands(R"([
        { "name": "cost", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "auto_cost" },
        { "name": "compute", "description": "", "resolver": "auto_cost" }
    ])");

    const CommandNode& d = commandAt(t, 0).node();

    ASSERT_TRUE(d.hasDeferred());
    EXPECT_FALSE(d.hasResolver());
    ASSERT_TRUE(d.hasConfig());
    EXPECT_EQ(d.enumIndex(), static_cast<uint16_t>(config::Ospf::REFERENCE_BANDWIDTH));
}

// The id is a position in the flattener's table, not a hash of the name, so the
// two halves of one key agree by construction rather than by luck.
TEST_F(Internal_CliTreeTest, OneKeyNumbersTheSameOnBothSides)
{
    CommandTree t = buildCommands(R"([
        { "name": "cost", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "auto_cost" },
        { "name": "compute", "description": "", "resolver": "auto_cost" }
    ])");

    const CommandNode& d = commandAt(t, 0).node();
    const CommandNode& r = commandAt(t, 1).node();

    ASSERT_TRUE(d.hasDeferred());
    ASSERT_TRUE(r.hasResolver());
    EXPECT_EQ(d.deferKey(), r.deferKey());
    EXPECT_EQ(d.deferKey(), 0u);
}

TEST_F(Internal_CliTreeTest, DistinctKeysGetDistinctIds)
{
    CommandTree t = buildCommands(R"([
        { "name": "a", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "key_a" },
        { "name": "ra", "description": "", "resolver": "key_a" },
        { "name": "b", "description": "",
          "config": "Ospf::SHUTDOWN", "deferred": "key_b" },
        { "name": "rb", "description": "", "resolver": "key_b" }
    ])");

    EXPECT_EQ(commandAt(t, 0).node().deferKey(), 0u);
    EXPECT_EQ(commandAt(t, 1).node().deferKey(), 0u);
    EXPECT_EQ(commandAt(t, 2).node().deferKey(), 1u);
    EXPECT_EQ(commandAt(t, 3).node().deferKey(), 1u);
}

// The key is what separates them, so a field claimed once by a plain command may
// still be named by several deferred ones.
TEST_F(Internal_CliTreeTest, SeveralDeferredCommandsMayShareOneField)
{
    CommandTree t = buildCommands(R"([
        { "name": "a", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "key_a" },
        { "name": "b", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "key_b" },
        { "name": "ra", "description": "", "resolver": "key_a" },
        { "name": "rb", "description": "", "resolver": "key_b" }
    ])");

    EXPECT_NE(commandAt(t, 0).node().deferKey(), commandAt(t, 1).node().deferKey());
}

TEST_F(Internal_CliTreeTest, DeferredWithoutAFieldIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "deferred": "k" },
        { "name": "r", "description": "", "resolver": "k" }
    ])"), std::runtime_error);
}

// Keys are discovered from the grammar rather than declared, so a name spelled
// two ways flattens as two keys. Neither half resolving is what catches it.
TEST_F(Internal_CliTreeTest, ADeferredKeyNothingResolvesIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "auto_cost" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, AResolverNothingDefersUnderIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "r", "description": "", "resolver": "orphan" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, AMisspelledKeyPairsWithNothing)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "auto_cost" },
        { "name": "r", "description": "", "resolver": "auto_cst" }
    ])"), std::runtime_error);
}

// deferKeyId holds one key, so a node cannot be both halves of one pairing.
TEST_F(Internal_CliTreeTest, DeferredAndResolverOnOneCommandIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "Ospf::REFERENCE_BANDWIDTH",
          "deferred": "a", "resolver": "b" }
    ])"), std::runtime_error);
}

// deferKeyId lives apart from configExt now, so a deferral no longer competes
// with an enum member for the same byte -- both are accepted on one node.
TEST_F(Internal_CliTreeTest, DeferredAlongsideAnEnumMemberIsAccepted)
{
    CommandTree t = buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea::AREA_TYPE",
          "enum": "AreaType::STUB", "deferred": "k" },
        { "name": "r", "description": "", "resolver": "k" }
    ])");

    const CommandNode& d = commandAt(t, 0).node();

    EXPECT_TRUE(d.hasDeferred());
    EXPECT_TRUE(d.hasEnumChange());
}

// Likewise for a mode change: entering a mode still claims configExt for the
// mode id, but deferring which key it waits on no longer costs it that byte.
TEST_F(Internal_CliTreeTest, DeferredAlongsideAModeChangeIsAccepted)
{
    CommandTree t = buildCommands(R"([
        { "name": "x", "description": "", "config": "Ospf::AREA_CONFIGS",
          "mode": "(config-router)#", "deferred": "k" },
        { "name": "r", "description": "", "resolver": "k" }
    ])");

    const CommandNode& d = commandAt(t, 0).node();

    EXPECT_TRUE(d.hasDeferred());
    EXPECT_TRUE(d.hasModeChange());
}

// A container binding is what a deferral is usually for: the key it holds says
// which entry to index, and holding it is the whole point of deferring.
TEST_F(Internal_CliTreeTest, DeferredOnAContainerIsAccepted)
{
    CommandTree t = buildCommands(R"([
        { "name": "x", "description": "",
          "config": "Ospf::AREA_CONFIGS", "deferred": "k" },
        { "name": "r", "description": "", "resolver": "k" }
    ])");

    const CommandNode& d = commandAt(t, 0).node();

    ASSERT_TRUE(d.hasDeferred());
    EXPECT_TRUE(d.hasConfig());
}

// The rescope itself is what waits. Flagging it where the word is typed would
// move the write target immediately, which is what the deferral puts off.
TEST_F(Internal_CliTreeTest, ADeferredContainerDoesNotRescopeInPlace)
{
    CommandTree t = buildCommands(R"([
        { "name": "x", "description": "",
          "config": "Ospf::AREA_CONFIGS", "deferred": "k" },
        { "name": "r", "description": "", "resolver": "k" }
    ])");

    EXPECT_FALSE(commandAt(t, 0).node().hasRegistryChange());
}

// An undeferred container still rescopes where it stands, as it always did.
TEST_F(Internal_CliTreeTest, AnUndeferredContainerStillRescopes)
{
    CommandTree t = buildCommands(R"([
        { "name": "x", "description": "", "config": "Ospf::AREA_CONFIGS" }
    ])");

    EXPECT_TRUE(commandAt(t, 0).node().hasRegistryChange());
}

// The other half stays rejected: a resolver binds no field of its own, so a
// container on one names nothing to rescope through.
TEST_F(Internal_CliTreeTest, AResolverOnAContainerIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "k" },
        { "name": "r", "description": "",
          "config": "Ospf::AREA_CONFIGS", "resolver": "k" }
    ])"), std::runtime_error);
}

TEST_F(Internal_CliTreeTest, AnEmptyDeferralKeyIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "" }
    ])"), std::runtime_error);
}

// Both flags sit in the high bits of the same uint16 every other property rides
// in, and the key shares configExt with the enum and tuple bindings.
TEST_F(Internal_CliTreeTest, DeferralSurvivesSerialization)
{
    CommandTree t(flattenCommands(R"([
        { "name": "cost", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "auto_cost" },
        { "name": "compute", "description": "", "resolver": "auto_cost" }
    ])"));

    const CommandNode& d = commandAt(t, 0).node();
    const CommandNode& r = commandAt(t, 1).node();

    EXPECT_TRUE(d.hasDeferred());
    EXPECT_TRUE(r.hasResolver());
    EXPECT_EQ(d.deferKey(), r.deferKey());
    EXPECT_TRUE(d.hasConfig());
}


// ===================================================================
// DEFERRED EXECUTION
//
// Drives whole lines through Executor::execute, which is the only way a
// deferral can be tested: nothing stages a key on its own any more. The line is
// reordered before it is dispatched, so each resolver follows the deferred word
// it answers, and what runs afterwards is an ordinary line.
// ===================================================================


// The line `area 5` on its own: the key is staged as the words go by, and the
// `<cr>` at the end is what indexes it.
TEST_F(Internal_CliTreeTest, ABareDeferredLineIndexesAtTheCr)
{
    OspfFixture f(AREA_GRAMMAR);

    Command area   = f.cmd(0);
    Command number = area.at(0);
    Command cr     = number.at(0);

    Line line;
    line.push("area", area);
    line.push("5", number);
    line.pushCr(cr);

    EXPECT_TRUE(f.exec.execute(line.tokens()));
    EXPECT_EQ(areaCount(f.reg), 1u);
}

// Nothing is written where the deferred command is typed. Without the resolver
// the key is dropped when execute returns, and the field keeps its default --
// which is what makes a deferral invisible to someone reading the config back.
TEST_F(Internal_CliTreeTest, WithoutTheResolverNothingIsIndexed)
{
    OspfFixture f(AREA_GRAMMAR);

    Command area   = f.cmd(0);
    Command number = area.at(0);

    Line line;
    line.push("area", area);
    line.push("5", number);

    f.exec.execute(line.tokens());
    EXPECT_EQ(areaCount(f.reg), 0u);
}

// A value command sitting between the key and the resolver. Both have to run:
// the value where it is typed, and the resolver at the end of the line.
//
// This is the case a run scanner gets wrong. A resolver binds no field on
// purpose, so it looks exactly like a trailing argument, and the value run in
// front of it swallowed it whole -- costing the resolver its turn in the loop,
// which is the only place a deferred key is ever written.
TEST_F(Internal_CliTreeTest, AValueRunDoesNotSwallowTheResolverBehindIt)
{
    OspfFixture f(AREA_GRAMMAR);

    Command area   = f.cmd(0);
    Command number = area.at(0);
    Command refbw  = number.at(1);
    Command value  = refbw.at(0);
    Command cr     = value.at(0);

    Line line;
    line.push("area", area);
    line.push("7", number);
    line.push("reference-bandwidth", refbw);
    line.push("1000", value);
    line.pushCr(cr);

    EXPECT_TRUE(f.exec.execute(line.tokens()));

    // The value landed where it was typed...
    EXPECT_EQ(f.reg.get<config::Ospf::REFERENCE_BANDWIDTH>().load(), 1000u);

    // ...and the key still reached its resolver.
    EXPECT_EQ(areaCount(f.reg), 1u);
}

// The same hole, reached through the toggle path rather than the value one: a
// bool field is its own command, and the `<cr>` behind it is still a resolver.
TEST_F(Internal_CliTreeTest, AToggleRunDoesNotSwallowTheResolverBehindIt)
{
    OspfFixture f(AREA_GRAMMAR);

    Command area   = f.cmd(0);
    Command number = area.at(0);
    Command lls    = number.at(2);
    Command cr     = lls.at(0);

    Line line;
    line.push("area", area);
    line.push("3", number);
    line.push("lls", lls);
    line.pushCr(cr);

    EXPECT_TRUE(f.exec.execute(line.tokens()));

    EXPECT_TRUE(f.reg.get<config::Ospf::LLS>().load());
    EXPECT_EQ(areaCount(f.reg), 1u);
}

// The rescope is confined to the line that asked for it. A second line starts
// from the registry the session is standing in, not from the entry the first
// line indexed.
TEST_F(Internal_CliTreeTest, TheRescopeDoesNotOutliveTheLine)
{
    OspfFixture f(AREA_GRAMMAR);

    Command area   = f.cmd(0);
    Command number = area.at(0);
    Command cr     = number.at(0);

    void* entry = f.ctx.ctx;

    Line first;
    first.push("area", area);
    first.push("1", number);
    first.pushCr(cr);
    ASSERT_TRUE(f.exec.execute(first.tokens()));

    EXPECT_EQ(f.ctx.ctx, entry);

    Line second;
    second.push("area", area);
    second.push("2", number);
    second.pushCr(cr);
    ASSERT_TRUE(f.exec.execute(second.tokens()));

    EXPECT_EQ(areaCount(f.reg), 2u);
}

// Two deferred commands on one line under the same key: the later spelling is
// the one that indexes, since each resolver pairs with the deferred word
// nearest in front of it.
TEST_F(Internal_CliTreeTest, TheLastKeyOnALineIsTheOneIndexed)
{
    OspfFixture f(AREA_GRAMMAR);

    Command area   = f.cmd(0);
    Command number = area.at(0);
    Command cr     = number.at(0);

    Line line;
    line.push("area", area);
    line.push("1", number);
    line.push("area", area);
    line.push("2", number);
    line.pushCr(cr);

    EXPECT_TRUE(f.exec.execute(line.tokens()));

    ASSERT_EQ(areaCount(f.reg), 1u);

    // Area 2 is the one that exists; asking for it does not grow the list.
    areaAt(f.reg, 2);
    EXPECT_EQ(areaCount(f.reg), 1u);
}

// Reaching the resolver is what says the command was meant, so the deferred
// word carrying no value still indexes: Key{} is a real entry rather than a
// failure, and `area` alone reaches area 0.
TEST_F(Internal_CliTreeTest, AnUnstagedKeyIndexesTheDefaultEntry)
{
    OspfFixture f(AREA_GRAMMAR);

    Command area = f.cmd(0);

    Line line;
    line.push("area", area);
    line.pushCr(area.at(0).at(0));

    EXPECT_TRUE(f.exec.execute(line.tokens()));

    ASSERT_EQ(areaCount(f.reg), 1u);

    // The default entry is area 0; asking for it does not grow the list.
    areaAt(f.reg, 0);
    EXPECT_EQ(areaCount(f.reg), 1u);
}

// A key that will not translate is a different matter from one that was never
// given: it takes the line no further rather than falling back to the default,
// and leaves the context where it was for nothing behind it to write into.
TEST_F(Internal_CliTreeTest, AKeyThatWillNotTranslateIndexesNothing)
{
    OspfFixture f(AREA_GRAMMAR);

    Command area   = f.cmd(0);
    Command number = area.at(0);

    Line line;
    line.push("area", area);
    line.push("not-a-number", number);
    line.pushCr(number.at(0));

    EXPECT_FALSE(f.exec.execute(line.tokens()));
    EXPECT_EQ(areaCount(f.reg), 0u);
    EXPECT_EQ(f.ctx.ctx, static_cast<void*>(&f.reg));
}

// A deferred command whose field is not an owned list has nothing to index. It
// refuses quietly: the grammar is checked when it is flattened, and what is left
// here are the shapes that check cannot see.
TEST_F(Internal_CliTreeTest, AFieldThatIsNotAnOwnedListRefusesQuietly)
{
    OspfFixture f(R"([
        { "name": "cost", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "cost",
          "subcommands": [
            { "name": "<cr>", "description": "", "resolver": "cost" }
          ] }
    ])");

    Command cost = f.cmd(0);

    Line line;
    line.push("100", cost);
    line.pushCr(cost.at(0));

    EXPECT_FALSE(f.exec.execute(line.tokens()));
    EXPECT_EQ(f.ctx.ctx, static_cast<void*>(&f.reg));
}

// ===================================================================
// DEFERRED/RESOLVER MERGE
//
// A resolver may now carry its own config, independently of the deferred
// word it pairs with -- see [[command-node-spare-flag-bits]]. Merging picks
// whichever of the two nodes is the more complete one to run; disagreement
// on any of config, pattern or value leaves them as two separate writes.
// ===================================================================

// The resolver names a different field than the deferred word. Nothing to
// choose between them, so both run: the deferred word writes its own field
// with its own value, and the resolver writes its own as an ordinary command.
TEST_F(Internal_CliTreeTest, AResolverNamingADifferentFieldRunsAlongsideTheDeferredWrite)
{
    OspfFixture f(R"([
        { "name": "cost", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "k",
          "subcommands": [
            { "name": "disable", "description": "",
              "config": "Ospf::SHUTDOWN", "resolver": "k" }
          ] }
    ])");

    Command cost    = f.cmd(0);
    Command disable = cost.at(0);

    Line line;
    line.push("100", cost);
    line.push("disable", disable);

    EXPECT_TRUE(f.exec.execute(line.tokens()));

    EXPECT_EQ(f.reg.get<config::Ospf::REFERENCE_BANDWIDTH>().load(), 100u);
    EXPECT_TRUE(f.reg.get<config::Ospf::SHUTDOWN>().load());
}

// The resolver names the same field the deferred word already does, and
// carries no value of its own beyond `<cr>`. Nothing conflicts and nothing is
// missing, so the deferred word's own value stands -- same outcome as before
// resolvers could carry config at all.
TEST_F(Internal_CliTreeTest, AResolverNamingTheSameFieldAgreesRatherThanConflicts)
{
    OspfFixture f(R"([
        { "name": "cost", "description": "",
          "config": "Ospf::REFERENCE_BANDWIDTH", "deferred": "k",
          "subcommands": [
            { "name": "<cr>", "description": "",
              "config": "Ospf::REFERENCE_BANDWIDTH", "resolver": "k" }
          ] }
    ])");

    Command cost = f.cmd(0);

    Line line;
    line.push("250", cost);
    line.pushCr(cost.at(0));

    EXPECT_TRUE(f.exec.execute(line.tokens()));
    EXPECT_EQ(f.reg.get<config::Ospf::REFERENCE_BANDWIDTH>().load(), 250u);
}
