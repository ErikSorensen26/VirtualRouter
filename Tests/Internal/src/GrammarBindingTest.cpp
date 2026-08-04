// Covers what the flattener does with the three keys that bind a command to
// config: "config" for a field, "config" with a third part for one member of a
// tuple valued field, and "enum" for a field set by naming an enum member.
//
// The grammar is stated inline per case rather than taken from
// VirtualRouter/commands, because the shipped grammar carries almost no
// bindings yet -- these exercise the resolution, not the data.

#include <gtest/gtest.h>
#include <cli/tree/CommandTree.h>
#include <cli/tree/nodes/Command.h>
#include <cli/tree/nodes/ModeEntry.h>
#include <configs/RegistryTable.hpp>
#include <configs/registry/router/OspfRegistry.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace cli::tree;

namespace
{

class TempGrammarDir
{
public:
    TempGrammarDir()
    {
        static std::atomic<unsigned> counter{0};
        dir = std::filesystem::temp_directory_path()
            / ("grammar-bind-" + std::to_string(::getpid())
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

// One mode holding the given command list, flattened.
CommandTree buildCommands(const std::string& commandsArray)
{
    TempGrammarDir dir;
    dir.write("Mode0",
        "{\n  \"prompt\": \"(config-router)#\",\n"
        "  \"commands\": " + commandsArray + "\n}");
    return CommandTree(parser::flattenDir(dir.path()));
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

}

class Internal_GrammarBindingTest : public ::testing::Test {};

// PLAIN FIELD BINDING

TEST_F(Internal_GrammarBindingTest, ConfigKeyBindsRegistryAndField)
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

TEST_F(Internal_GrammarBindingTest, UnknownRegistryIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "NoSuchRegistry::FIELD" }
    ])"), std::runtime_error);
}

TEST_F(Internal_GrammarBindingTest, UnknownFieldIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea::NO_SUCH_FIELD" }
    ])"), std::runtime_error);
}

TEST_F(Internal_GrammarBindingTest, RegistryWithoutFieldIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea" }
    ])"), std::runtime_error);
}

// Two commands writing the same plain field would each claim its one slot.
TEST_F(Internal_GrammarBindingTest, FieldBoundTwiceIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "a", "description": "", "config": "OspfArea::AREA_TYPE" },
        { "name": "b", "description": "", "config": "OspfArea::AREA_TYPE" }
    ])"), std::runtime_error);
}

// ENUM BINDING

TEST_F(Internal_GrammarBindingTest, EnumKeyResolvesToTheMemberValue)
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

TEST_F(Internal_GrammarBindingTest, EachMemberGetsItsOwnValue)
{
    CommandTree t = buildCommands(R"([
        { "name": "normal", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::NORMAL" },
        { "name": "nssa", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::TOTALLY_NSSA" }
    ])");

    EXPECT_EQ(commandAt(t, 0).node().configExt,
              static_cast<uint8_t>(config::ospf::AreaType::NORMAL));
    EXPECT_EQ(commandAt(t, 1).node().configExt,
              static_cast<uint8_t>(config::ospf::AreaType::TOTALLY_NSSA));
}

// Several commands set the same enum field to different members, which is the
// normal shape -- so unlike a plain field they must not collide on its slot.
TEST_F(Internal_GrammarBindingTest, EnumMembersShareOneFieldWithoutColliding)
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

TEST_F(Internal_GrammarBindingTest, EnumNamingTheWrongTypeIsRejected)
{
    // Resolves as a member name, but of an enum this field does not hold.
    EXPECT_THROW(buildCommands(R"([
        { "name": "stub", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "Duplex::STUB" }
    ])"), std::runtime_error);
}

TEST_F(Internal_GrammarBindingTest, EnumNamingAnAbsentMemberIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::NO_SUCH_MEMBER" }
    ])"), std::runtime_error);
}

// COUNT bounds the enum rather than being a value, so naming it would write an
// out of range value into the field.
TEST_F(Internal_GrammarBindingTest, EnumNamingCountIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "AreaType::COUNT" }
    ])"), std::runtime_error);
}

TEST_F(Internal_GrammarBindingTest, EnumOnANonEnumFieldIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "OspfArea::RANGE", "enum": "AreaType::STUB" }
    ])"), std::runtime_error);
}

// The field is where the enum's type is checked, so without one there is
// nothing to check against.
TEST_F(Internal_GrammarBindingTest, EnumWithoutAConfigKeyIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "enum": "AreaType::STUB" }
    ])"), std::runtime_error);
}

TEST_F(Internal_GrammarBindingTest, EnumMustNameBothTypeAndMember)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "OspfArea::AREA_TYPE", "enum": "STUB" }
    ])"), std::runtime_error);
}

// TUPLE MEMBER BINDING

TEST_F(Internal_GrammarBindingTest, ThirdKeyPartResolvesATupleMember)
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

TEST_F(Internal_GrammarBindingTest, TupleMembersResolveToTheirOwnPositions)
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
TEST_F(Internal_GrammarBindingTest, TupleMembersShareOneFieldWithoutColliding)
{
    EXPECT_NO_THROW(buildCommands(R"([
        { "name": "prefix", "description": "", "config": "OspfArea::RANGE::prefix" },
        { "name": "cost", "description": "", "config": "OspfArea::RANGE::cost" }
    ])"));
}

TEST_F(Internal_GrammarBindingTest, UnknownTupleMemberIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea::RANGE::nosuchmember" }
    ])"), std::runtime_error);
}

// A field with no TUPLE_SCHEMA_FOR names no members at all, so a member on one
// is a grammar mistake rather than a lookup miss.
TEST_F(Internal_GrammarBindingTest, TupleMemberOnANonTupleFieldIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea::AREA_TYPE::cost" }
    ])"), std::runtime_error);
}

TEST_F(Internal_GrammarBindingTest, FourPartConfigKeyIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "config": "OspfArea::RANGE::cost::extra" }
    ])"), std::runtime_error);
}

// CONFIGEXT IS SHARED

// A mode, an enum and a tuple member all store into configExt, so a command may
// be only one of the three. Left unchecked the last key parsed would win and
// the other binding would read back as something it is not.
TEST_F(Internal_GrammarBindingTest, EnumAndTupleMemberTogetherAreRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "",
          "config": "OspfArea::RANGE::cost", "enum": "AreaType::STUB" }
    ])"), std::runtime_error);
}

// ROUND TRIP

// The bindings live in the packed node, so they have to survive serialization
// rather than only being right in the builder's own memory.
TEST_F(Internal_GrammarBindingTest, BindingsSurviveSerialization)
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
TEST_F(Internal_GrammarBindingTest, RuntimeTupleLookupMatchesTheTypedOne)
{
    constexpr uint16_t reg = config::registryIdV<config::OspfArea>;
    constexpr uint16_t fld = static_cast<uint16_t>(config::OspfArea::RANGE);

    static_assert(config::findTupleMemberAt(config::RegistryEntries{}, reg, fld,
                      config::tokenHash("cost")) == 2);
    static_assert(config::findTupleMemberAt(config::RegistryEntries{}, reg, fld,
                      config::tokenHash("nope")) == config::TUPLE_NOT_FOUND);
    SUCCEED();
}

TEST_F(Internal_GrammarBindingTest, RuntimeEnumLookupChecksTheFieldsType)
{
    constexpr uint16_t reg = config::registryIdV<config::OspfArea>;
    constexpr uint16_t fld = static_cast<uint16_t>(config::OspfArea::AREA_TYPE);

    constexpr config::EnumResolution good = config::resolveEnumAt(
        config::RegistryEntries{}, reg, fld,
        config::tokenHash("AreaType"), config::tokenHash("STUB"));

    static_assert(good.fieldIsEnum);
    static_assert(good.typeMatched);
    static_assert(good.index == static_cast<uint16_t>(config::ospf::AreaType::STUB));

    // Right member name, wrong enum: caught by the type half rather than
    // resolving against whatever table happened to hold a "STUB".
    constexpr config::EnumResolution wrongType = config::resolveEnumAt(
        config::RegistryEntries{}, reg, fld,
        config::tokenHash("Duplex"), config::tokenHash("STUB"));

    static_assert(wrongType.fieldIsEnum);
    static_assert(!wrongType.typeMatched);
    static_assert(wrongType.index == config::ENUM_NOT_FOUND);

    SUCCEED();
}

TEST_F(Internal_GrammarBindingTest, NonEnumFieldReportsItselfAsSuch)
{
    constexpr uint16_t reg = config::registryIdV<config::OspfArea>;
    constexpr uint16_t fld = static_cast<uint16_t>(config::OspfArea::RANGE);

    constexpr config::EnumResolution res = config::resolveEnumAt(
        config::RegistryEntries{}, reg, fld,
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

TEST_F(Internal_GrammarBindingTest, ArgumentReachesTheVariablesLeaf)
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

TEST_F(Internal_GrammarBindingTest, EveryExpandedSiblingGetsTheArgument)
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

TEST_F(Internal_GrammarBindingTest, UnpassedArgumentLeavesTheNodeUnbound)
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

TEST_F(Internal_GrammarBindingTest, ArgumentsForwardThroughNestedVariables)
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

TEST_F(Internal_GrammarBindingTest, ArgumentCarriesAModeAndEnumToo)
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

TEST_F(Internal_GrammarBindingTest, NonObjectArgsIsRejected)
{
    EXPECT_THROW(buildWithVariables(
        R"([ { "name": "<kinds>", "description": "", "args": "nope" } ])",
        R"({ "kinds": [ { "name": "stub", "description": "" } ] })"),
        std::runtime_error);
}

TEST_F(Internal_GrammarBindingTest, NonStringArgumentValueIsRejected)
{
    EXPECT_THROW(buildWithVariables(
        R"([ { "name": "<kinds>", "description": "", "args": { "config": 7 } } ])",
        R"({ "kinds": [ { "name": "stub", "description": "" } ] })"),
        std::runtime_error);
}

TEST_F(Internal_GrammarBindingTest, DuplicateArgumentNameIsRejected)
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
TEST_F(Internal_GrammarBindingTest, ExitAndEndAreModeExitsByName)
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
TEST_F(Internal_GrammarBindingTest, AnOrdinaryCommandIsNotAModeExit)
{
    CommandTree t = buildCommands(R"([
        { "name": "exited", "description": "" }
    ])");

    EXPECT_FALSE(commandAt(t, 0).node().hasModeExit());
}

// The flag alone is enough, where hasModeChange() also wants configExt. An exit
// names no mode, so requiring one would make every exit read as not-an-exit.
TEST_F(Internal_GrammarBindingTest, ModeExitNeedsNoBoundField)
{
    CommandTree t = buildCommands(R"([
        { "name": "exit", "description": "", "properties": ["mode_exit"] }
    ])");

    const CommandNode& n = commandAt(t, 0).node();

    EXPECT_TRUE(n.hasModeExit());
    EXPECT_FALSE(n.hasConfig());
    EXPECT_EQ(n.configExt, CommandNode::CONFIG_EXT_NONE);
}

TEST_F(Internal_GrammarBindingTest, ExitingAndEnteringAModeIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "properties": ["mode_exit"],
          "config": "OspfArea::AREA_TYPE", "mode": "(config-router)#" }
    ])"), std::runtime_error);
}


TEST_F(Internal_GrammarBindingTest, UnknownPropertyIsRejected)
{
    EXPECT_THROW(buildCommands(R"([
        { "name": "x", "description": "", "properties": ["mode_exyt"] }
    ])"), std::runtime_error);
}
