/**
 * @file TraversalContext.hpp
 * @brief The state of one walk through the command tree, one word at a time.
 *
 * Parsing a CLI line is a walk: each word selects a node, and the node's
 * children become the candidates for the next word. @ref cli::TraversalContext
 * is the cursor and the bookkeeping for a single such walk — where the walk is,
 * what the last word matched, and whether the line is still viable.
 *
 * ## Matching a word
 * A word is resolved against the current node's children in two passes.
 * Keywords first, by name, exact before prefix: `int` reaches `interface`.
 * Only if nothing matches by name does @ref cli::TraversalContext::matchPattern
 * run, which tests the word against placeholder nodes — `WORD`, `A.B.C.D`,
 * `<0-9>` — by validating the value rather than comparing text.
 *
 * That order matters, and so does the exclusion: a placeholder's name describes
 * what may be typed there and is never itself typeable. Comparing a word against
 * it by name would let the literal text `A.B.C.D` satisfy the address it stands
 * for, so @ref cli::TraversalContext::validateCommand rules placeholders out of
 * name matching entirely and leaves them to @ref cli::TraversalContext::matchPattern.
 *
 * ## Why an exact match is not the end of it
 * `ip` is a whole command and also the start of `ipv6`. For execution the exact
 * match wins outright — typing `ip` runs `ip`. For help and tab it does not:
 * `ip?` is asking what else begins that way. Both are recorded, the winner in
 * `matchNode` and every candidate in `prefixMatches`, so neither reading has to
 * be reconstructed from the other.
 *
 * ## Repeat sets
 * Some grammar nodes may be re-entered. A `recursive` sibling is spent once
 * used, so after `metric 100` the set returns without `metric`; a
 * `subcmd_sequence` returns whole except for children marked
 * `subcmd_single_use`. @ref cli::TraversalContext::RepeatFrame tracks which
 * siblings are spent with a bitmask, which caps a set at
 * @ref cli::tree::MAX_TRACKED_SIBLINGS members.
 *
 * ## Input mode
 * The same walk serves execution, `?`, and tab, and they disagree about what is
 * an error. An ambiguous prefix is fatal to a command being run and is the
 * expected case for help, so `inputMode` gates the error rather than each caller
 * re-deciding.
 *
 * Views borrow from the command tree and the input line; a context must not
 * outlive either.
 */

#ifndef TRAVERSAL_CONTEXT_HPP
#define TRAVERSAL_CONTEXT_HPP

#include <regex>

#include <Global.h>

#include "CliSession.h"
#include "CliEngine.h"
#include "CliUtils.h"
#include "cli/session/Token.hpp"
#include "cli/tree/nodes/Command.h"
#include "cli/tree/nodes/ModeEntry.h"

namespace cli
{
inline static bool isVolatile(std::string_view p)
{
    return matchVolatilePattern(p) != P_NONE;
}

struct TraversalContext
{
    CliEngine& engine;

    bool negateMode = false;
    bool defaultMode = false;

    enum class MatchState
    {
        NONE, PATTERN, 
        PARTIAL, EXACT
    } matchState = MatchState::NONE;

    enum class CommandState
    {
        IN_PROGRESS, COMPLETE,
        INVALID, AMBIGUOUS
    } commandState = CommandState::IN_PROGRESS;

    enum class InputMode
    {
        NORMAL, HELP,
        TAB, LINE
    } inputMode = InputMode::NORMAL;


    tree::ModeEntry& currentMode;
    tree::Command currentDirectory;

    /**
     * A repeatable set the walk returns to once a branch ends.
     *
     * `recursive` siblings are each usable once: after "metric 100" completes,
     * the set comes back without "metric". `subcmd_sequence` returns the whole
     * set apart from the children marked `subcmd_single_use`, so unmarked
     * options stay available for as long as the user keeps typing.
     */
    struct RepeatFrame
    {
        tree::Command set;          // The sibling set to return to.
        uint64_t used = 0;          // Bit per consumed sibling.
        bool sequence = false;      // subcmd_sequence, else recursive.
    };
    std::vector<RepeatFrame> repeatStack;

    std::string_view previousMatch;
    std::string_view currentPattern;
    std::string_view endCmdStr;

    /**
     * @brief The node the end-of-command marker matched, where one did.
     *
     * The marker itself carries no binding, but the `<cr>` leaf it stands for is
     * an ordinary node with its own flags, and a `resolver` is spelled there:
     * the end of the line is the one point that means the command is finished
     * rather than merely passed through. Kept beside @ref endCmdStr, which
     * already records the same node's name, so the token built from the marker
     * can carry the node instead of resolving nothing.
     */
    tree::Command endCmdNode;

    /**
     * @brief Every candidate the last word is a prefix of.
     *
     * Includes an exact match and anything extending it, so "ip" yields both
     * `ip` and `ipv6`. Help and tab list these; execution instead takes the
     * single winner, since typing `ip` should run `ip` rather than report an
     * ambiguity with `ipv6`.
     */
    std::vector<tree::Command> prefixMatches;

    bool eoc = false; // End of Command
    bool err = false; // Error
    bool nwh = false; // Next Word Help

    TraversalContext(CliEngine& e, tree::ModeEntry& mode)
        : engine(e), currentMode(mode), currentDirectory(mode.commands())
    {}

    bool isRunning()         const { return commandState == CommandState::IN_PROGRESS; }
    bool isCommandValid()    const { return commandState == CommandState::COMPLETE; }
    bool isCommandInvalid()  const { return commandState == CommandState::INVALID; }
    bool isHelpActive()      const { return inputMode == InputMode::HELP || inputMode == InputMode::TAB; }
    bool isLineActive()      const { return inputMode == InputMode::LINE; }
    bool isMatchSuccessful() const { return matchState == MatchState::EXACT || matchState == MatchState::PATTERN; }
    bool isMatchExact()      const { return matchState == MatchState::EXACT; }
    bool isPatternMatching() const { return matchState == MatchState::PATTERN; }

    std::vector<std::string_view> tokenize(const std::string& line)
    {
        std::vector<std::string_view> tokens;
        size_t start = 0;
        bool inToken = false;

        auto pushToken = [&](size_t end)
        {
            if (inToken)
            {
                tokens.emplace_back(line.data() + start, end - start);
                inToken = false;
            }
        };

        for (size_t i = 0; i < line.size(); ++i)
        {
            char ch = line[i];

            if (ch == '?' || ch == '\t')
            {
                pushToken(i);
                tokens.emplace_back(&line[i], 1);
                return tokens;
            }

            if (std::isspace(static_cast<unsigned char>(ch)))
            {
                pushToken(i);
            }
            else
            {
                if (!inToken)
                {
                    start = i;
                    inToken = true;
                }
            }
        }

        pushToken(line.size());
        return tokens;
    }

    void extendLineToken(const std::string& line, std::string_view& token, bool help)
    {
        size_t offset = static_cast<size_t>(token.data() - line.data());
        if (help) offset++; // Increment to shave help command off the end
        if (offset > line.size()) return;
        token = std::string_view(line.data() + offset, line.size() - offset);
    }

    std::vector<tree::Command> availableAt(std::string_view userInput, std::vector<tree::Command>& prevCommands)
    {
        tree::Command* prevCmd = prevCommands.empty() ? nullptr : &prevCommands.front();

        if (err) return {};

        eoc = false;
        endCmdNode = {};

        bool addCarriage = false;
        if ((negateMode || defaultMode) && prevCmd)
        {
            bool negateAll = false;
            addCarriage = hasProp(*prevCmd, tree::CommandNode::NEGATE,
                                  tree::CommandNode::NEGATE_ALL, negateAll);

            if (negateAll) return {{}};
        }

        bool inRepeat = false;
        std::vector<tree::Command> repeatRest;
        if (!repeatStack.empty() && currentDirectory.hasExclusiveCR())
        {
            repeatRest = repeatOptions();
            inRepeat = true;
        }

        std::vector<tree::Command> available;
        std::optional<tree::Command> matchNode;
        bool hasExact = false;
        int matchCount = 0;

        std::vector<tree::Command> candidates;
        if (inRepeat) candidates = repeatRest;
        else for (const auto& c : currentDirectory)
            candidates.push_back(c);

        const bool isMarker = userInput == "?" || userInput == "\t";
        if (!isMarker) prefixMatches.clear();

        for (const auto& cmd : candidates)
        {
            if (shouldHide(cmd, negateMode || defaultMode)) continue;

            std::string_view cmdName = cmd.name();

            MatchState state = validateCommand(cmdName, userInput);

            if (state >= MatchState::PATTERN && cmdName.size() >= userInput.size())
            {
                if (state >= MatchState::PARTIAL && !hasExact)
                    matchNode = cmd, ++matchCount;
                if (state == MatchState::EXACT)
                    hasExact = true;

                if (state >= MatchState::PARTIAL && !isMarker) prefixMatches.push_back(cmd);
            }
            available.push_back(cmd);
        }

        if (matchCount == 0 && !hasExact)
        {
            for (const auto& cmd : candidates)
            {
                std::string_view name = cmd.name();
                if (matchPattern(userInput, name))
                {
                    matchNode = cmd;
                    matchCount = 1;
                    if (cmd.size() == 0) { eoc = true; endCmdNode = cmd; }
                    break;
                }
            }
        }

        if (inRepeat) available.push_back({});
        if (addCarriage) available.push_back({});
        if (hasExact) matchCount = 1;

        if (matchCount == 1)
        {
            const tree::Command& owner = inRepeat ? repeatStack.back().set : currentDirectory;
            size_t ordinal = owner.find(matchNode->name());
            if (ordinal != tree::Command::NPOS)
            {
                if (inRepeat)
                {
                    if (ordinal < tree::MAX_TRACKED_SIBLINGS)
                        repeatStack.back().used |= (uint64_t{1} << ordinal);
                }
                else markRepeatUsed(owner, ordinal);
            }

            if (matchNode->size() != 0)
                currentDirectory = matchNode.value();

            commandState = currentDirectory.hasCarriageReturn() || matchNode->size() == 0
                         ? CommandState::COMPLETE
                         : CommandState::IN_PROGRESS;

            if (hasExact) return { matchNode.value() };
        }
        else if (!isMatchSuccessful() && (matchCount != 1 || matchNode->size() != 0))
        {
            if (!(isHelpActive() && matchCount > 1))
                err = true;
        }
        else if (hasExact && matchNode->size() == 0 && !userInput.empty() &&
            !((negateMode && userInput == "no") || (defaultMode && userInput == "default")))
        {
            endCmdStr = matchNode->name();
            endCmdNode = matchNode.value();
            eoc = true;
            return {};
        }
        else { eoc = false; endCmdNode = {}; }

        if (!matchCount && !userInput.empty() && err && userInput != "?" && userInput != "\t")
            return {};
        // matchCount of 0 means nothing matched, so there is no node to inspect.
        if (!matchCount && (!matchNode || matchNode->size() == 0)
            && userInput != "?" && userInput != "\t" && !isPatternMatching())
            return {};
        return available;
    }

    bool matchPattern(std::string_view input, std::string_view pattern)
    {
        if (previousMatch.empty() || input == "?" || input == "\t")
            return false;

        auto accept = [&](bool lineMode = false)
        {
            currentPattern = pattern;
            matchState = MatchState::PATTERN;
            if (lineMode) inputMode = InputMode::LINE;
        };

        // WORD
        if (pattern == "WORD" && matchWord(input, previousMatch))
        {
            accept();
            return true;
        }

        // LINE
        if (pattern == "LINE")
        {
            accept(true);
            return true;
        }

        // IPv4
        if (pattern == "A.B.C.D" && cli::utils::isIPv4Address(input))
        {
            accept();
            return true;
        }

        // IPv4 with a prefix length
        if (pattern == "A.B.C.D/nn" && cli::utils::isIPv4AddressWithMask(input))
        {
            accept();
            return true;
        }

        // IPv6
        if ((pattern == "X:X:X:X::X" && cli::utils::isIPv6Address(input)) ||
            (pattern == "X:X:X:X::X/<0-128>" && cli::utils::isIPv6AddressWithMask(input)))
        {
            accept();
            return true;
        }

        // MAC
        if (pattern == "H.H.H" && cli::utils::isMACAddress(input))
        {
            accept();
            return true;
        }

        // Numeric range.
        if (cli::utils::matchNumericRange(input, pattern))
        {
            accept();
            return true;
        }

        return false;
    }

private:

    // AVAILABLE AT HELPERS

    /**
     * @brief Reads a negate property off @p cmd.
     *
     * @param cmd  Command node to inspect.
     * @param a    The plain property, e.g. @c NEGATE.
     * @param aAll The whole-subtree variant, e.g. @c NEGATE_ALL.
     * @param[out] all Set when @p aAll is present: the command negates its whole
     *                 subtree at once, so the walk ends here and no children follow.
     * @return Whether @p a alone is present.
     */
    static bool hasProp(const tree::Command& cmd, tree::CommandNode::Property a,
                        tree::CommandNode::Property aAll, bool& all)
    {
        if (cmd.hasProp(aAll)) { all = true; return false; }
        all = false;
        return cmd.hasProp(a);
    }

    static bool shouldHide(const tree::Command& cmd, bool negate)
    {
        return (negate && cmd.hasProp(tree::CommandNode::NEGATE_HIDE)) ||
               (!negate && cmd.hasProp(tree::CommandNode::NEGATE_SHOW));
    }

    static bool isRepeatable(const tree::Command& set, bool& sequence)
    {
        if (set.hasProp(tree::CommandNode::SUBCMD_SEQUENCE)) { sequence = true; return true; }
        for (const auto& c : set)
            if (c.hasProp(tree::CommandNode::RECURSIVE)) { sequence = false; return true; }
        return false;
    }

public:
    void markRepeatUsed(const tree::Command& set, size_t ordinal)
    {
        bool sequence = false;
        if (!isRepeatable(set, sequence)) return;
        if (ordinal >= tree::MAX_TRACKED_SIBLINGS) return;

        // A sequence keeps one frame for the whole run; recursive sets nest.
        if (!repeatStack.empty() && repeatStack.back().sequence && sequence)
        {
            repeatStack.back().used |= (uint64_t{1} << ordinal);
            return;
        }
        repeatStack.push_back({set, uint64_t{1} << ordinal, sequence});
    }

    /**
     * @brief The still-available members of the innermost repeat set.
     *
     * Empty when no set is active. A recursive set drops every sibling already
     * used; a sequence drops only those marked `subcmd_single_use`, since its
     * unmarked options may be repeated.
     */
    std::vector<tree::Command> repeatOptions() const
    {
        if (repeatStack.empty()) return {};
        const RepeatFrame& f = repeatStack.back();

        std::vector<tree::Command> out;
        for (size_t i = 0; i < f.set.size(); ++i)
        {
            bool used = i < tree::MAX_TRACKED_SIBLINGS && (f.used & (uint64_t{1} << i));
            tree::Command child = f.set.at(i);

            if (used)
            {
                // Recursive members are one-shot. In a sequence only the
                // single-use ones are spent; the rest stay on offer.
                if (!f.sequence) continue;
                if (child.hasProp(tree::CommandNode::SUBCMD_SINGLE_USE)) continue;
            }
            out.push_back(child);
        }
        return out;
    }

private:

    static MatchState validateCommand(std::string_view cmdName, std::string_view raw)
    {
        if (isVolatile(cmdName))
            return MatchState::PATTERN;
        if (utils::lowerCmp(raw, cmdName))
            return MatchState::EXACT;
        else if (utils::partialLowerCmp(raw, cmdName))
            return MatchState::PARTIAL;
        return MatchState::NONE;
    }

    // MATCH PATTERN HELPERS

    static bool matchWord(std::string_view input, std::string_view previousMatch)
    {
        static const std::regex hostnameRx  { R"(^[A-Za-z0-9]([A-Za-z0-9\-\.]*[A-Za-z0-9])?$)" };
        static const std::regex nameRx      { R"(^[A-Za-z0-9\-]+$)" };
        static const std::regex passwordRx  { R"(^[ -~]+$)" };
        static const std::regex filenameRx  { R"(^[A-Za-z0-9_\-\.\/]+$)" };
        static const std::regex communityRx { R"(^[0-9]+:[0-9]+$)" };
        static const std::regex wordRx      { R"(^[A-Za-z0-9_\-\.\/]+$)" };

        static const std::unordered_map<std::string_view, const std::regex*> volatileWordMap {
            { "hostname", &hostnameRx }, { "name", &nameRx }, { "vrf", &nameRx }, { "route-map", &nameRx },
            { "policy-map", &nameRx }, { "group", &nameRx }, { "class", &nameRx }, { "pool", &nameRx },
            { "context", &nameRx }, { "vdpn-group", &nameRx },
            { "password", &passwordRx }, { "secret", &passwordRx }, { "key-string", &passwordRx },
            { "encryption type", &passwordRx },
            { "filename", &filenameRx }, { "flash", &filenameRx }, { "tftp", &filenameRx },
            { "dir", &filenameRx }, { "view", &filenameRx },
            { "community", &communityRx }, { "as number", &communityRx }
        };

        const std::regex* rx = &wordRx; // Default
        auto it = volatileWordMap.find(previousMatch);
        if (it != volatileWordMap.end()) rx = it->second;

        return std::regex_match(input.begin(), input.end(), *rx);
    }
};
}

#endif // TRAVERSAL_CONTEXT_HPP
