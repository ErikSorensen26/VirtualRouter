// Every command in the shipped grammar, run against the live config, generated
// rather than written out.
//
// The hand-written suites (EigrpConfigTest, CliTest) each cover one mode with a
// list of lines someone typed by hand. That scales badly and rots quietly: a
// node added to a mode nobody listed is a node nothing runs, and a binding that
// stops writing looks exactly like a binding that was never tested.
//
// So this walks the tree instead. For every mode in the mode table it enumerates
// the root-to-<cr> paths under it, synthesizes an argument for each placeholder
// on the way down, stands a session in that mode, and runs the line. What it
// then checks is the part that matters: a command claiming to bind a config
// field has to have *changed* that field.
//
// The check is a registry snapshot, not a per-field assertion. Every field of
// every registry the command could reach is hashed before and after the line;
// if a command binds config and the hash is unchanged, it wrote nothing. That
// avoids naming fields one by one -- which is what makes this survive grammar
// edits -- while still catching the failure the hand-written tests catch.
//
// Failures are reported per command line, so a break names the line and the
// mode rather than "the generated test failed".

class Internal_GeneratedCommandTest;

#include <gtest/gtest.h>
#include <MockConsole.hpp>
#include <MockFileSystem.hpp>
#include <cli/session/CliEngine.h>
#include <cli/session/CliSession.h>
#include <cli/session/CliUtils.h>
#include <cli/session/Configs.h>
#include <cli/session/Token.hpp>
#include <cli/modes/Mode.hpp>
#include <cli/tree/CommandTree.h>
#include <cli/tree/nodes/Command.h>
#include <cli/tree/nodes/ModeEntry.h>
#include <core/Global.h>
#include <configs/RegistryTable.hpp>

#include <algorithm>
#include <charconv>
#include <string>
#include <vector>

using cli::CliEngine;
using cli::CliMode;
using cli::CliSession;
using cli::FileSystem;
using cli::MockFileSystem;
using cli::ReducedMockConsole;
using core::Global;
using cli::tree::Command;
using cli::tree::CommandNode;
using cli::tree::CommandTree;
using cli::tree::ModeEntry;

namespace
{

// ---------------------------------------------------------------------------
// Argument synthesis
// ---------------------------------------------------------------------------

// A value the parser will accept for a placeholder node.
//
// The values are deliberately boring except for numeric ranges, which take the
// declared low bound. The bounds are where an off-by-one in the range check
// lives, and the low bound is the one that is always valid -- a range whose high
// bound overflows its field would otherwise fail here for the wrong reason.
std::string synthesizeArg(std::string_view placeholder, cli::Pattern pattern)
{
    switch (pattern)
    {
        case cli::P_WORD:     return "WORDVAL";
        case cli::P_LINE:     return "some free text";
        case cli::P_IPV4:     return "10.0.0.1";
        case cli::P_IPV6:     return "2001:db8::1";
        case cli::P_IPV6PFX:  return "2001:db8::/32";
        case cli::P_IPV4PFX:  return "10.0.0.0/24";
        case cli::P_MAC:      return "0011.2233.4455";
        case cli::P_NUMRNG:
        {
            // "<lo-hi>" -- take lo. A dotted low bound ("<1.0-XX.YY>") is an AS
            // in dotted notation, which the numeric path cannot spell, so those
            // are handed back verbatim for the caller to skip.
            const size_t dash = placeholder.find('-', 1);
            if (dash == std::string_view::npos) return std::string(placeholder);

            std::string_view lo = placeholder.substr(1, dash - 1);
            if (lo.find('.') != std::string_view::npos) return std::string(placeholder);

            return std::string(lo);
        }
        default:
            return std::string(placeholder);
    }
}

// True when the node is a placeholder whose value cannot be synthesized, so any
// line through it would fail for a reason that is not a binding defect.
bool isUnsynthesizable(std::string_view name, cli::Pattern pattern)
{
    if (pattern == cli::P_NUMRNG)
    {
        // A dotted-notation range; see synthesizeArg.
        const size_t dash = name.find('-', 1);
        return dash == std::string_view::npos
            || name.substr(1, dash - 1).find('.') != std::string_view::npos;
    }

    if (pattern != cli::P_NONE) return false;

    // No pattern matched, so the parser will take this node's name as a literal
    // keyword and the generated word is that name typed back. That is right for
    // a keyword and wrong for a placeholder the matcher does not recognize:
    // "Hostname or A.B.C.D" and "/nn or A.B.C.D" are prose describing what to
    // type, and typing the prose parses as several bogus words.
    //
    // A keyword is one word of ordinary command text, so anything carrying a
    // space or a placeholder's shape is skipped rather than echoed.
    if (name.find(' ') != std::string_view::npos) return true;
    if (name.find('/') != std::string_view::npos) return true;

    // A.B.C.D, H.H.H, X:X::X -- upper-case letters joined by separators, which
    // no keyword in the grammar spells.
    const bool hasUpper = std::any_of(name.begin(), name.end(),
                                      [](char c) { return c >= 'A' && c <= 'Z'; });
    const bool hasSep = name.find('.') != std::string_view::npos
                     || name.find(':') != std::string_view::npos;

    return hasUpper && hasSep;
}

// ---------------------------------------------------------------------------
// Path enumeration
// ---------------------------------------------------------------------------

// One generated command: the text to type, and what the grammar says it does.
struct GeneratedLine
{
    std::string text;

    // True when some node on the path binds a config field, which is what makes
    // the line's effect checkable. A line of pure keywords writes nothing by
    // design and is only checked for parsing.
    bool bindsConfig = false;

    // True when the line enters or leaves a mode, so the runner knows to put the
    // session back before the next line rather than letting it drift.
    bool movesMode = false;

    // True when the line enters a mode specifically. Kept apart from movesMode
    // because a line that only leaves one -- a bare `exit` -- has no round trip
    // to make, and asking it to come back is asking it to undo its whole job.
    bool entersMode = false;

    // True when a node on the path is negate_show: hidden from the plain form and
    // offered only under `no`. Such a line is spelled with the prefix or it does
    // not resolve at all -- `ipv6 enable` is not a command, `no ipv6 enable` is.
    bool needsNegate = false;
};

// Walks every root-to-<cr> path under `node`, appending a line for each.
//
// Depth and breadth are both capped. The shipped grammar has nodes with a
// hundred-odd children (`redistribute` and its protocol sub-grammars), and the
// full cross product is neither runnable nor more informative than a sample --
// the point is to reach every *node*, which a bounded walk still does.
void collectPaths(const Command& node, std::vector<std::string>& words,
                  std::vector<GeneratedLine>& out,
                  bool boundConfig, bool movedMode, bool enteredMode, bool negateOnly,
                  size_t depth)
{
    static constexpr size_t MAX_DEPTH    = 12;
    static constexpr size_t MAX_CHILDREN = 24;
    static constexpr size_t MAX_LINES    = 20000;

    if (depth > MAX_DEPTH || out.size() >= MAX_LINES) return;

    const size_t n = std::min(node.size(), MAX_CHILDREN);

    for (size_t i = 0; i < n; ++i)
    {
        Command child = node.at(i);
        if (!child.valid()) continue;

        const std::string_view name = child.name();

        // "<cr>" is the end marker rather than a word: reaching it means the
        // words collected so far are a complete command.
        if (name == "<cr>")
        {
            if (words.empty()) continue;

            // A path that binds no config and moves no mode is a command the
            // grammar declares and nothing implements -- `ip host WORD A.B.C.D`
            // is spelled out in full and has no config key on any node of it.
            // There is nothing to assert about such a line: it writes nothing by
            // construction, so running it tests the parser against a command
            // that was never wired up.
            if (!boundConfig && !movedMode) continue;

            // The prefix goes on here rather than at the call site: whether the
            // line needs it is only known once the whole path has been walked.
            std::string text = negateOnly ? "no" : "";
            for (const std::string& w : words)
            {
                if (!text.empty()) text += ' ';
                text += w;
            }
            out.push_back({std::move(text), boundConfig, movedMode, enteredMode, negateOnly});
            continue;
        }

        const cli::Pattern pattern = cli::matchVolatilePattern(name);
        if (isUnsynthesizable(name, pattern)) continue;

        const CommandNode& raw = child.node();

        words.push_back(synthesizeArg(name, pattern));
        collectPaths(child, words, out,
                     boundConfig || raw.hasConfig(),
                     movedMode || raw.hasModeChange() || raw.hasModeExit(),
                     enteredMode || raw.hasModeChange(),
                     negateOnly || child.hasProp(CommandNode::NEGATE_SHOW),
                     depth + 1);
        words.pop_back();
    }
}

// ---------------------------------------------------------------------------
// Registry snapshotting
// ---------------------------------------------------------------------------

// A cheap order-sensitive mix. Only equality between two snapshots matters, so
// this needs to be stable within a run rather than good.
inline void mixInto(uint64_t& h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}

// Hashes every field of the registry a context points at.
//
// A command's write can land anywhere in the registry the mode configures, and
// naming the field per command is exactly the hand-maintenance this file exists
// to avoid -- so the whole registry is hashed and any change counts.
//
// Reads go through the same accessors the executor writes through, which is why
// this is a visit rather than a memcmp: a field's storage may be a pointer to a
// heap list whose bytes do not move when the list grows.
template <typename ENUM>
uint64_t hashRegistry(void* ptr)
{
    if (!ptr) return 0;

    uint64_t h = 0xcbf29ce484222325ull;
    auto& reg = *static_cast<config::RegistryOfT<ENUM>*>(ptr);

    for (size_t i = 0; i < config::registrySlotsV<ENUM>; ++i)
    {
        reg.visit(i, [&](auto&& accessor)
        {
            using A = std::remove_cvref_t<decltype(accessor)>;

            mixInto(h, i);

            // An owned list: the identity of the children is the state, and the
            // children are registries of their own rather than values.
            //
            // Only the child pointers are mixed, not the keys. Keys are whatever
            // the field declares -- a uint16_t AS, a string VRF name, an
            // InterfaceKey -- and there is no one conversion covering all of
            // them. The pointers already distinguish "a child was added" from
            // "nothing happened", which is the question being asked; a mutation
            // *inside* a child is a write to that child's own registry and is
            // seen when the session is standing in it.
            if constexpr (requires { accessor.get().size(); })
            {
                mixInto(h, accessor.get().size());

                // The map's order is not stable across rehashes, so the pointers
                // are combined commutatively rather than mixed in sequence.
                uint64_t members = 0;
                for (const auto& [k, v] : accessor.get())
                {
                    (void)k;
                    members += reinterpret_cast<uintptr_t>(v);
                }
                mixInto(h, members);
            }
            // A list field, read under its own lock.
            else if constexpr (requires { accessor.readEach([](auto&){}); })
            {
                mixInto(h, accessor.size());
            }
            // A scalar that may be unset; hasValue is itself state.
            else if constexpr (requires { accessor.hasValue(); accessor.load(); })
            {
                mixInto(h, accessor.hasValue() ? 1 : 0);
                if (accessor.hasValue())
                {
                    auto v = accessor.load();
                    using V = std::remove_cvref_t<decltype(v)>;

                    if constexpr (std::is_convertible_v<V, std::string_view>)
                    {
                        const std::string_view sv{v};
                        mixInto(h, sv.size());
                        for (char c : sv) mixInto(h, static_cast<unsigned char>(c));
                    }
                    else if constexpr (requires { static_cast<uint64_t>(v); })
                    {
                        mixInto(h, static_cast<uint64_t>(v));
                    }
                    else
                    {
                        const auto* bytes = reinterpret_cast<const unsigned char*>(&v);
                        for (size_t b = 0; b < sizeof(v); ++b) mixInto(h, bytes[b]);
                    }
                }
            }
            else if constexpr (requires { static_cast<uint64_t>(accessor.load()); })
            {
                mixInto(h, static_cast<uint64_t>(accessor.load()));
            }
            else if constexpr (requires { accessor.load(); })
            {
                auto v = accessor.load();
                using V = std::remove_cvref_t<decltype(v)>;

                // A string's bytes are a pointer, a length and an SSO buffer --
                // and load() returns a temporary, so the pointer is a fresh
                // allocation on every call. Hashing the storage would compare
                // two unrelated addresses; the characters are the state.
                if constexpr (std::is_convertible_v<V, std::string_view>)
                {
                    const std::string_view sv{v};
                    mixInto(h, sv.size());
                    for (char c : sv) mixInto(h, static_cast<unsigned char>(c));
                }
                else
                {
                    // A value whose type is not an integer -- a tuple, an
                    // address. Its bytes are the value itself, so they can be
                    // mixed directly.
                    const auto* bytes = reinterpret_cast<const unsigned char*>(&v);
                    for (size_t b = 0; b < sizeof(v); ++b) mixInto(h, bytes[b]);
                }
            }
        });
    }

    return h;
}

// Dispatches hashRegistry on a registry id known only at runtime.
uint64_t hashContext(const cli::ContextBase& ctx)
{
    if (ctx.ctxRegistry == cli::ContextBase::NO_REGISTRY) return 0;

    uint64_t h = 0;
    config::forEachRegistryId(config::RegistryEntries{}, [&]<typename Entry>()
    {
        using ENUM = typename Entry::type;
        if constexpr (config::hasRegistryV<ENUM>)
            if (config::registryIdV<ENUM> == ctx.ctxRegistry)
                h = hashRegistry<ENUM>(ctx.ctx);
    });

    return h;
}

}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class Internal_GeneratedCommandTest : public ::testing::Test
{
public:
    testing::NiceMock<ReducedMockConsole>* mockConsole = nullptr;
    static MockFileSystem* mockFileSystem;
    static FileSystem* realFileSystem;
    static CliEngine* engine;
    static Global* global;

protected:
    CliSession* terminal = nullptr;

    static std::string commandTreeString;
    static std::string configFileString;

    static void SetUpTestSuite()
    {
        realFileSystem = new FileSystem();
        mockFileSystem = new testing::NiceMock<MockFileSystem>();

        if (realFileSystem->fileExists("./" + std::string(COMMAND_TREE)))
            realFileSystem->readFile("./" + std::string(COMMAND_TREE), commandTreeString);
        else
            FAIL() << "Failed to open the command tree file: " << COMMAND_TREE;

        if (realFileSystem->fileExists("./" + std::string(HW_CONFIG_FILE)))
            realFileSystem->readFile("./" + std::string(HW_CONFIG_FILE), configFileString);
        else
            FAIL() << "Failed to open the hardware config file: " << HW_CONFIG_FILE;

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
        terminal->changeMode(CliMode::PrivilegedExec, global->configs);
        terminal->changeMode(CliMode::GlobalConfiguration, global->configs);
        mockConsole->resetCapturedOutput();
    }

    void TearDown() override
    {
        // Dropped before the session, which may still point into one of them.
        scratchRegistries.clear();
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

public:
    bool handleInput(const std::string& command) { return terminal->handleInput(command); }

    bool parsesOk(std::string cmd)
    {
        // executeCommand clears these before every parse, and parseInput reads
        // them while walking. Calling parseInput directly without clearing them
        // leaks the previous line's `no` into this one.
        terminal->context.negate    = false;
        terminal->context.defaulted = false;

        return terminal->parseInput(cmd).status == CliSession::ParseResult::Status::OK_;
    }

    // Why a line was refused, for the failure report. INCOMPLETE and AMBIGUOUS
    // are different findings -- one means the path stopped short of <cr>, the
    // other that two siblings both matched -- and the distinction is most of the
    // diagnosis.
    const char* parseStatusName(std::string cmd)
    {
        terminal->context.negate    = false;
        terminal->context.defaulted = false;

        using S = CliSession::ParseResult::Status;
        switch (terminal->parseInput(cmd).status)
        {
            case S::EMPTY:      return "EMPTY";
            case S::OK_:        return "OK";
            case S::HELP:       return "HELP";
            case S::TAB:        return "TAB";
            case S::INVALID:    return "INVALID";
            case S::AMBIGUOUS:  return "AMBIGUOUS";
            case S::INCOMPLETE: return "INCOMPLETE";
            case S::GLOBAL_CMD: return "GLOBAL_CMD";
            case S::DO_COMMAND: return "DO_COMMAND";
        }
        return "?";
    }

    // Friendship does not reach the class GTest derives per TEST_F, so the
    // session's private state is republished here rather than reached into.
    const cli::ContextBase& sessionContext() const { return terminal->context; }
    CliMode currentMode() { return terminal->getMode(); }

    // Every generated line for one mode, plus what it takes to stand in it.
    //
    // The mode is kept as a CliMode rather than a table index: a line written
    // for (config-dhcp)# only parses with the session standing in that mode, and
    // changeMode is the only way to put it there.
    struct ModeLines
    {
        CliMode mode = CliMode::None;
        std::string modeName;
        uint16_t registryId = cli::tree::ModeEntryNode::REGISTRY_NONE;
        std::vector<GeneratedLine> lines;
    };

    // Walks every mode the enum names, generating that mode's lines.
    //
    // Driven from CliMode rather than from the mode table because getMode maps
    // name to row and there is no reverse: a table row on its own cannot say
    // which CliMode would enter it, and entering is the point.
    std::vector<ModeLines> generateAll()
    {
        CommandTree& tree = engine->getCommandTree();
        std::vector<ModeLines> all;

        for (size_t m = 0; m < static_cast<size_t>(CliMode::Count); ++m)
        {
            const CliMode mode = static_cast<CliMode>(m);
            if (mode == CliMode::None) continue;

            ModeEntry entry;
            try
            {
                entry = tree.getMode(mode);
            }
            catch (const std::exception&)
            {
                // A CliMode the shipped grammar declares no command list for.
                continue;
            }

            ModeLines ml;
            ml.mode = mode;
            ml.registryId = entry.hasRegistry()
                ? entry.registryId()
                : cli::tree::ModeEntryNode::REGISTRY_NONE;

            ml.modeName = std::string(entry.name());
            if (!entry.subName().empty())
                ml.modeName += "/" + std::string(entry.subName());

            std::vector<std::string> words;
            collectPaths(entry.commands(), words, ml.lines, false, false, false, false, 0);

            all.push_back(std::move(ml));
        }

        return all;
    }

    // Stands the session in @p ml, over a registry of the type that mode
    // configures, and returns false when there is no such registry to build.
    //
    // The registries are heap-allocated and kept for the run rather than
    // stack-scoped: a line may enter a further mode whose parent pointer is one
    // of these, and a freed parent would be read after the line.
    bool standIn(const ModeLines& ml)
    {
        // A mode naming no registry cannot be stood in by allocating one. Falling
        // back to GlobalConfiguration would be worse than refusing: the session
        // would be in the wrong mode and every line written for this one would
        // fail to parse, reported as a grammar defect it is not.
        if (ml.registryId == cli::tree::ModeEntryNode::REGISTRY_NONE)
            return ml.mode == CliMode::GlobalConfiguration
                && terminal->changeMode(CliMode::GlobalConfiguration, global->configs);

        if (ml.mode == CliMode::GlobalConfiguration)
            return terminal->changeMode(CliMode::GlobalConfiguration, global->configs);

        bool ok = false;
        config::forEachRegistryId(config::RegistryEntries{}, [&]<typename Entry>()
        {
            using ENUM = typename Entry::type;
            if constexpr (config::hasRegistryV<ENUM>)
            {
                using Reg = config::RegistryOfT<ENUM>;
                if (config::registryIdV<ENUM> != ml.registryId || ok) return;

                // Only registries that can stand alone are usable here. One that
                // needs a parent is reached by entering its parent's mode first,
                // which the mode-change lines already do.
                if constexpr (std::is_default_constructible_v<Reg>)
                {
                    auto owned = std::make_unique<Reg>();
                    Reg& reg = *owned;
                    scratchRegistries.push_back(
                        std::shared_ptr<void>(owned.release(),
                                              [](void* p) { delete static_cast<Reg*>(p); }));
                    ok = terminal->changeMode(ml.mode, reg);
                }
            }
        });

        return ok;
    }

    void clearScratch() { scratchRegistries.clear(); }

private:
    std::vector<std::shared_ptr<void>> scratchRegistries;
};

CliEngine*      Internal_GeneratedCommandTest::engine         = nullptr;
FileSystem*     Internal_GeneratedCommandTest::realFileSystem = nullptr;
MockFileSystem* Internal_GeneratedCommandTest::mockFileSystem = nullptr;
Global*         Internal_GeneratedCommandTest::global         = nullptr;
std::string     Internal_GeneratedCommandTest::commandTreeString;
std::string     Internal_GeneratedCommandTest::configFileString;

// ---------------------------------------------------------------------------
// The generator itself, checked before it is trusted
// ---------------------------------------------------------------------------

// A generator that produced nothing would make every test below vacuously pass,
// so its output is asserted on before anything is run through it.
TEST_F(Internal_GeneratedCommandTest, GeneratorProducesLinesForManyModes)
{
    const auto all = generateAll();

    size_t modesWithLines = 0;
    size_t total = 0;
    for (const auto& ml : all)
    {
        if (!ml.lines.empty()) ++modesWithLines;
        total += ml.lines.size();
    }

    EXPECT_GT(modesWithLines, 5u) << "the walk reached almost no modes";
    EXPECT_GT(total, 200u) << "the walk produced too few lines to mean anything";
}

// The synthesized values have to be values the parser accepts, or every
// execution failure below would be an artifact of this file rather than a
// finding about the grammar.
TEST_F(Internal_GeneratedCommandTest, SynthesizedArgumentsMatchTheirPlaceholders)
{
    EXPECT_EQ(synthesizeArg("<1-65535>", cli::P_NUMRNG), "1");
    EXPECT_EQ(synthesizeArg("<0-4294967295>", cli::P_NUMRNG), "0");
    EXPECT_EQ(synthesizeArg("WORD", cli::P_WORD), "WORDVAL");
    EXPECT_EQ(synthesizeArg("A.B.C.D", cli::P_IPV4), "10.0.0.1");

    // Dotted AS notation cannot be spelled numerically and is skipped rather
    // than guessed at.
    EXPECT_TRUE(isUnsynthesizable("<1.0-XX.YY>", cli::P_NUMRNG));
    EXPECT_FALSE(isUnsynthesizable("<1-65535>", cli::P_NUMRNG));
}

// ---------------------------------------------------------------------------
// The generated checks
// ---------------------------------------------------------------------------

// Every generated line resolves to a runnable command.
//
// This is the parser half, and it is the weaker of the two: a line that fails
// here is a grammar defect -- a placeholder the matcher does not recognise, or
// a sibling shadowing another -- rather than a binding one.
// Names each mode the generator produced lines for, whether a session can be
// stood in it, and how many lines it carries.
//
// Not an assertion so much as the map the two generated checks below are read
// against: a mode reporting "cannot stand" explains its lines being skipped, and
// one reporting a registry it could not build names the registry to look at.
TEST_F(Internal_GeneratedCommandTest, ReportsWhichModesCanBeStoodIn)
{
    const auto all = generateAll();

    for (const auto& ml : all)
    {
        if (ml.lines.empty()) continue;

        const bool stood = standIn(ml);
        std::cout << (stood ? "  stand   " : "  CANNOT  ")
                  << ml.modeName
                  << "  registry=" << ml.registryId
                  << "  lines=" << ml.lines.size();

        if (stood) std::cout << "  landedIn=" << static_cast<int>(currentMode());

        std::cout << "\n";
    }

    SUCCEED();
}

TEST_F(Internal_GeneratedCommandTest, EveryGeneratedLineParses)
{
    const auto all = generateAll();

    size_t checked = 0;
    std::vector<std::string> failures;

    for (const auto& ml : all)
    {
        for (const GeneratedLine& line : ml.lines)
        {
            // Re-stand the session for each line: a previous line may have
            // entered a mode, and the next line is written for this one. A mode
            // with no standable registry is skipped, not failed -- it is reached
            // by entering its parent, which the mode tests already cover.
            if (!standIn(ml)) break;

            if (!parsesOk(line.text) && failures.size() < 40)
                failures.push_back(ml.modeName + ": " + line.text
                                   + "  [" + parseStatusName(line.text) + "]");

            ++checked;
        }
    }

    EXPECT_GT(checked, 0u);

    // Reported together rather than one EXPECT per line: a grammar regression
    // breaks lines in bulk, and forty of them scrolling past is less useful
    // than forty named in one place.
    EXPECT_TRUE(failures.empty())
        << failures.size() << " generated lines did not parse, first few:\n"
        << [&] {
               std::string s;
               for (const std::string& f : failures) s += "  " + f + "\n";
               return s;
           }();
}

// The point of the file: a command that binds a config field has to change it.
//
// A line that parses, executes, reports success, and leaves the registry exactly
// as it found it has a broken binding -- the config key names a field that is
// never reached, or the value never translated. That failure is invisible to the
// parser and invisible to handleInput's return, and this is what sees it.
TEST_F(Internal_GeneratedCommandTest, EveryBindingCommandChangesTheConfig)
{
    const auto all = generateAll();

    std::vector<std::string> silent;
    size_t checked = 0;

    for (const auto& ml : all)
    {
        for (const GeneratedLine& line : ml.lines)
        {
            // Only lines the grammar says write something are checkable. A mode
            // change moves the context out from under the snapshot, so those are
            // covered by the mode tests rather than here.
            if (!line.bindsConfig || line.movesMode) continue;

            // A `no` line clears a field, and on a registry standing at its
            // defaults there is nothing to clear -- writing nothing is the
            // correct outcome, so it proves nothing either way.
            if (line.needsNegate) continue;

            if (!standIn(ml)) break;

            const cli::ContextBase& ctx = sessionContext();
            const uint64_t before = hashContext(ctx);

            const bool ran = handleInput(line.text);

            const uint64_t after = hashContext(ctx);
            ++checked;

            // A line that did not run is the parse test's finding, not this
            // one's -- reporting it twice would bury the silent writes.
            if (!ran) continue;

            if (before == after && silent.size() < 40)
                silent.push_back(ml.modeName + ": " + line.text);
        }
    }

    EXPECT_GT(checked, 0u) << "no binding lines were reachable to check";

    EXPECT_TRUE(silent.empty())
        << silent.size() << " commands bind a config field but wrote nothing:\n"
        << [&] {
               std::string s;
               for (const std::string& f : silent) s += "  " + f + "\n";
               return s;
           }();
}

// A mode-changing line has to actually move the session, and `exit` has to bring
// it back to where it started.
//
// This is the shape that crashed: `router eigrp 1` rescopes into the VRF and
// enters a mode in one line, and the return pointer and the write target are
// different registries. A round trip that lands anywhere but home means the nav
// frame kept the wrong one.
TEST_F(Internal_GeneratedCommandTest, ModeChangesRoundTripBackToWhereTheyStarted)
{
    const auto all = generateAll();

    std::vector<std::string> stuck;
    size_t checked = 0;

    for (const auto& ml : all)
    {
        for (const GeneratedLine& line : ml.lines)
        {
            if (!line.entersMode) continue;

            if (!standIn(ml)) break;

            const cli::ContextBase& ctx = sessionContext();
            void* const    homePtr = ctx.ctx;
            const uint16_t homeReg = ctx.ctxRegistry;

            if (!handleInput(line.text)) continue;
            ++checked;

            // Only lines that actually entered somewhere have a trip to make.
            if (currentMode() == ml.mode) continue;

            handleInput("exit");

            if ((ctx.ctx != homePtr || ctx.ctxRegistry != homeReg)
                && stuck.size() < 40)
            {
                stuck.push_back(ml.modeName + ": " + line.text);
            }
        }
    }

    EXPECT_GT(checked, 0u) << "no mode-changing lines were reachable to check";

    EXPECT_TRUE(stuck.empty())
        << stuck.size() << " mode changes did not return to their entry target:\n"
        << [&] {
               std::string s;
               for (const std::string& f : stuck) s += "  " + f + "\n";
               return s;
           }();
}

// Every context the session ever stands in has to know which registry it points
// at. An untagged one is a void* the executor casts blind, which is the crash
// this suite was written around.
TEST_F(Internal_GeneratedCommandTest, EveryEnteredModeCarriesItsRegistryTag)
{
    const auto all = generateAll();

    std::vector<std::string> untagged;

    for (const auto& ml : all)
    {
        for (const GeneratedLine& line : ml.lines)
        {
            if (!line.entersMode) continue;

            if (!standIn(ml)) break;
            if (!handleInput(line.text)) continue;
            if (currentMode() == ml.mode) continue;

            const cli::ContextBase& ctx = sessionContext();
            if (ctx.ctx != nullptr
                && ctx.ctxRegistry == cli::ContextBase::NO_REGISTRY
                && untagged.size() < 40)
            {
                untagged.push_back(ml.modeName + ": " + line.text);
            }
        }
    }

    EXPECT_TRUE(untagged.empty())
        << untagged.size() << " modes were entered with an untagged context:\n"
        << [&] {
               std::string s;
               for (const std::string& f : untagged) s += "  " + f + "\n";
               return s;
           }();
}
