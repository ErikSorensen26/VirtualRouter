// CliSession.cpp

#include <algorithm>
#include <cctype>
#include <sstream>

#include <Global.h>

#include "CliSession.h"
#include "CliEngine.h"
#include "cli/session/Token.hpp"
#include "cli/modes/Mode.hpp"
#include "TraversalContext.hpp"

namespace cli
{
static std::string lowerStr(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return static_cast<unsigned char>(std::tolower(c)); });
    return s;
}

static std::string trimLeft(const std::string& s)
{
    auto it = std::find_if(s.begin(), s.end(),
        [](unsigned char c){ return !std::isspace(c); });
    return std::string(it, s.end());
}

static std::string_view trimRight(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

static std::string getLastWord(const std::string& s)
{
    std::istringstream ss(s);
    std::string w, last;
    while (ss >> w) last = w;
    return last;
}

CliSession::CliSession(CliEngine& engine, ConsoleController& controller, bool enableDebug)
    : Console(controller),
      engine(engine),
      context(*this),
      commandTree(engine.getCommandTree()),
      nav(commandTree, context),
      executor(nav, context)
{
    nav.changeMode(CliMode::GlobalConfiguration, engine.global.getConfigs());
    initConsole();

#ifdef DEBUG
    isDebugModeEnabled = enableDebug;
#endif

    controller.print("Initializing Terminal...\r\n");
}

void CliSession::handlePrompt()
{
    setPrompt(engine.global.getHostname() + std::string(nav.getPrompt()));

    insert = false;
    insertString.clear();
    cursorPos = 0;

    std::string preload;
    if (!nextLine.empty())
    {
        preload = nextLine;
        cursorPos = preload.size();
        renderInput(preload);
        nextLine.clear();
    }
    inputCacheBuffer = preload;
}

bool CliSession::handleInput(std::string test)
{
    std::string userCommand = input(test, !paginationList.empty());

    if (userCommand == "CRT-Z" && getMode() != CliMode::UserExec)
    {
        if (!nav.resetAndChangeMode(CliMode::PrivilegedExec, engine.global.getConfigs()))
        {
            controller.print("\r\n");
            return false;
        }
    }

    if (!paginationList.empty())
    {
        handlePagination(userCommand.empty() ? '\x20' : userCommand[0]);
        return true;
    }

    if (!executeCommand(userCommand))
    {
        if (!paginationList.empty()) return false;
        controller.print("\r\n");
        handlePrompt();
        return false;
    }

    if (!paginationList.empty())
    {
        handlePagination();
        return false;
    }

    controller.print("\r\n");
    handlePrompt();
    return true;
}

CliSession::ParseResult CliSession::parseInput(std::string& rawInput)
{
    ParseResult result;
    TraversalContext ctx(engine, nav.getCurrentMode());

    std::vector<std::string_view> words = ctx.tokenize(rawInput);
    if (words.empty()) return result;

    // Detect help or tab taken
    const bool hasHelpToken = words.back() == "?" || words.back() == "\t";

    if (hasHelpToken)
        ctx.inputMode = words.back() == "?" ? TraversalContext::InputMode::HELP
                                            : TraversalContext::InputMode::TAB;

    if (hasHelpToken)
    {
        const size_t markerOff = static_cast<size_t>(words.back().data() - rawInput.data());
        ctx.nwh = markerOff == 0 || std::isspace(static_cast<unsigned char>(rawInput[markerOff - 1]));
    }

    // Handle "no" and "default"
    auto handlePrefix = [&](const std::string& prefix, bool& flag)
    {
        if (!utils::lowerCmp(words[0], prefix) || words.size() < 2) return;
        const CliMode m = nav.getMode();
        if (m != CliMode::UserExec && m != CliMode::PrivilegedExec)
        {
            flag = true;
        }
    };

    handlePrefix("no", context.negate);
    handlePrefix("default", context.defaulted);

    if (!hasHelpToken && words.size() >= 2 && utils::partialLowerCmp(words[0], DO_EXEC_KEYWORD))
    {
        const size_t after = static_cast<size_t>(words[0].data() - rawInput.data())
                           + words[0].size();
        const size_t pos = rawInput.find_first_not_of(" \t", after);
        result.status = ParseResult::Status::DO_COMMAND;
        result.doRemainder = (pos != std::string::npos) ? rawInput.substr(pos) : "";
        return result;
    }

    // if (words[0] == "?" || words[0] == "\t") ctx.matchState = MatchState::EXACT;

    std::vector<Token> tokens;
    std::vector<tree::Command> prevCommands;

    for (size_t idx = 0; idx < words.size(); idx++)
    {
        std::string_view word = words[idx];
        
        ctx.matchState = TraversalContext::MatchState::NONE;
        std::vector<tree::Command> available = ctx.availableAt(word, prevCommands);

        const size_t markerPos = static_cast<size_t>(word.data() - rawInput.data());

        // Help
        if (word == "?")
        {
            std::vector<tree::Command>& helpList = ctx.nwh ? available : ctx.prefixMatches;

            result.nextLine = rawInput.substr(0, markerPos);
            if (helpList.empty())
                controller.print("\r\n% Unrecognized Command");
            result.helpList = helpList;
            result.status = ParseResult::Status::HELP;
            return result;
        }

        // Tab
        if (word == "\t")
        {
            const bool oneCandidate = ctx.prefixMatches.size() == 1;

            const bool completable = oneCandidate && !isVolatile(ctx.prefixMatches[0].name());

            if (ctx.nwh || prevCommands.size() != 1 || !completable)
            {
                result.nextLine = rawInput.substr(0, markerPos);
                result.status = ParseResult::Status::TAB;
                return result;
            }

            // Single match autocomplete
            std::string_view stripped = std::string_view(rawInput).substr(0, markerPos);
            long lastSp = static_cast<long>(stripped.rfind(' '));
            std::string nl;
            if (lastSp < 0)
                nl = std::string(prevCommands[0].name()) + " ";
            else
                nl = std::string(stripped.substr(0, static_cast<size_t>(lastSp + 1))) + std::string(prevCommands[0].name()) + " ";
            result.nextLine = nl;
            result.status = ParseResult::Status::TAB;
            return result;
        }

        // Error handle
        if (ctx.err && !ctx.isHelpActive())
        {
            if (available.size() > 1)
            {
                result.status = ParseResult::Status::AMBIGUOUS;
                result.ambiguousToken = word;
                return result;
            }
            result.status = ParseResult::Status::INVALID;
            result.markerCommand = trimLeft(rawInput.substr(0, static_cast<size_t>(word.data() - rawInput.data())));
            return result;
        }

        // Match available commands
        std::vector<tree::Command> matches;
        for (const auto& cmd : available)
        {
            if (ctx.isPatternMatching() && cmd.name() == ctx.currentPattern)
                matches.push_back(cmd);
            else if (!isVolatile(cmd.name()) &&
                     cmd.name().size() >= word.size() && utils::partialLowerCmp(word, cmd.name()))
                matches.push_back(cmd);
        }

        prevCommands = matches.empty() ? available : matches;

        // Append token
        if (matches.size() <= 1)
        {
            if (ctx.isPatternMatching() && !matches.empty())
            {
                if (result.valueStart == tree::CommandTree::NPOS)
                    result.valueStart = tokens.size();

                // LINE takes the rest of the input as one free-text value.
                const bool lineToken = matchVolatilePattern(matches[0].name()) == P_LINE;
                const std::string_view value = lineToken
                    ? trimRight(std::string_view(rawInput).substr(markerPos))
                    : word;

                tokens.emplace_back(value, matches[0]);
                ctx.previousMatch = matches[0].name();

                if (lineToken) break;
            }
            else if (matches.size() == 1)
            {
                tokens.emplace_back(matches[0].name(), matches[0]);
                ctx.previousMatch = matches[0].name();
            }
            else if (matches.empty() && ctx.eoc)
            {
                // Synthesized end-of-command marker; no node resolved it.
                tokens.emplace_back(ctx.endCmdStr);
            }
            else if (matches.empty())
            {
                tokens.emplace_back(word);
            }
            else
            {
                tokens.emplace_back(matches[0].name(), matches[0]);
                ctx.previousMatch = matches[0].name();
            }
        }
        else
        {
            tokens.emplace_back(word);
        }

        if (idx == 0)
        {
            if (utils::lowerCmp(word, "no") && context.negate) ctx.currentDirectory = ctx.currentMode.commands();
            if (utils::lowerCmp(word, "default") && context.defaulted) ctx.currentDirectory = ctx.currentMode.commands();
        }
    }

    if (ctx.eoc) ctx.commandState = TraversalContext::CommandState::COMPLETE;

    // Final validation
    if (ctx.isHelpActive() && ctx.isRunning())
    {
        result.status = ParseResult::Status::HELP;
        return result;
    }

    if (!ctx.isCommandValid() && !hasHelpToken)
    {
        if (prevCommands.size() > 1)
        {
            result.status = ParseResult::Status::AMBIGUOUS;
            result.ambiguousToken = getLastWord(rawInput);
            return result;
        }
        result.status = ParseResult::Status::INCOMPLETE;
        return result;
    }

    result.status = ParseResult::Status::OK_;
    result.tokens = std::move(tokens);
    return result;
}

bool CliSession::executeCommand(std::string& command, bool quiet)
{
    isModeChanged = false;
    textLine      = false;
    context.negate   = false;
    context.defaulted = false;

    if (command.empty()) return false;

    ParseResult parsed = parseInput(command);

    switch (parsed.status)
    {
        case ParseResult::Status::EMPTY:
            return false;

        case ParseResult::Status::HELP:
            if (!parsed.helpList.empty())
                displayCommands(parsed.helpList);
            nextLine = parsed.nextLine;
            return true;

        case ParseResult::Status::TAB:
            nextLine = parsed.nextLine;
            return true;

        case ParseResult::Status::INVALID:
        {
            if (tryGlobalCommand(command)) return true;

            if (quiet) return false;

            const std::string marker =
                "\r\n"
                + std::string(initialLineLength + parsed.markerCommand.size(), ' ')
                + "^\r\n% Invalid input detected at '^' marker.\r\n";
            controller.print(marker);
            return false;
        }

        case ParseResult::Status::AMBIGUOUS:
            if (quiet) return false;
            controller.print("\r\n% Ambiguous command: \"" + parsed.ambiguousToken + "\"");
            return false;

        case ParseResult::Status::INCOMPLETE:
            if (tryGlobalCommand(command)) return true;
            if (quiet) return false;
            controller.print("\r\n% Incomplete Command");
            return false;

        case ParseResult::Status::GLOBAL_CMD:
            return true;

        case ParseResult::Status::DO_COMMAND:
            return tryDoCommand(parsed.doRemainder);

        case ParseResult::Status::OK_:
            break;
    }

    // Set textLine from token patterns (LINE already collapsed in parseInput)
    textLine = std::any_of(parsed.tokens.begin(), parsed.tokens.end(),
        [](const Token& t){ return t.isLine(); });

    const bool skipPrefix = (context.negate || context.defaulted) && parsed.tokens.size() > 1;
    std::span<Token> execTokens = skipPrefix
        ? std::span<Token>(parsed.tokens).subspan(1)
        : std::span<Token>(parsed.tokens);

    if (execTokens.empty()) return false;

    return executor.execute(execTokens);
}

bool CliSession::tryDoCommand(const std::string& remainder)
{
    if (!nav.saveAndChangeMode(CliMode::PrivilegedExec, engine.global.getConfigs()))
        return false;

    std::string cmd = remainder;
    const bool ok   = executeCommand(cmd);
    nav.restore();

    return ok;
}

bool CliSession::tryGlobalCommand(const std::string& rawInput)
{
    const CliMode curMode = getMode();
    if (curMode == CliMode::GlobalConfiguration
        || curMode == CliMode::UserExec
        || curMode == CliMode::PrivilegedExec)
        return false;
    if (lowerStr(rawInput) == "exit")
        return false;

    nav.saveAndChangeMode(CliMode::GlobalConfiguration, engine.global.getConfigs());

    std::string cmd = rawInput;
    const bool ok   = executeCommand(cmd, /*quiet=*/true);

    const CliMode landed = getMode();
    void* const   landedCtx = nav.getContext().ctx;
    nav.restore();

    if (landed != CliMode::GlobalConfiguration)
        nav.changeMode(landed, landedCtx);

    return ok;
}

CliMode CliSession::getMode()
{
    return nav.getMode();
}

void CliSession::displayCommands(std::vector<tree::Command>& list)
{
    paginationList = list;
    maxNameLength  = 0;
    for (const auto& c : paginationList)
        if (c.name().size() > maxNameLength) maxNameLength = c.name().size();
}

bool CliSession::handlePagination(char nextch)
{
    if (nextch != '\0')
    {
        maxCommandLength = getTerminalWidth() - initialLineLength;

        if (nextch == '\x20')
        {
            controller.print("\033[2K\033[1G");
            controller.print("\033[1A");
        }
        else
        {
            controller.print("\033[2K\033[1G");
            controller.print(std::string(10, ' '));
            controller.print("\033[2K\033[1G");
            paginationList.clear();
            handlePrompt();
            return false;
        }
    }

    const size_t pageSize  = engine.paginationCount == 0
        ? paginationList.size() : engine.paginationCount;
    const size_t termWidth = getTerminalWidth();
    const size_t descCol   = 2 + maxNameLength + 6;

    for (size_t i = 0; i < paginationList.size() && i < pageSize; ++i)
    {
        const tree::Command& cmd = paginationList[i];
        const std::string_view name = cmd.name();
        std::string display = std::string("\r\n  ") + std::string(name);
        for (size_t j = 0; j <= (maxNameLength - name.size() + 5); ++j)
            display += ' ';

        const std::string_view desc = cmd.desc();
        if (desc.empty() || termWidth == 0 || descCol + desc.size() <= termWidth)
        {
            display += desc;
        }
        else
        {
            std::string indent(descCol, ' ');
            size_t col = descCol, di = 0;
            while (di < desc.size())
            {
                size_t wordStart = di;
                while (di < desc.size() && desc[di] != ' ') ++di;
                size_t wordEnd = di;
                while (di < desc.size() && desc[di] == ' ') ++di;

                std::string w(desc.substr(wordStart, wordEnd - wordStart));
                if (col + w.size() > termWidth && col > descCol)
                {
                    display += "\r\n" + indent;
                    col = descCol;
                }
                display += w;
                col += w.size();
                if (di < desc.size()) { display += ' '; ++col; }
            }
        }

        /*Color color;
        switch (cmd.support)
        {
            case Com::Support::SUPPORTED:  color = Color::WHITE;  break;
            case Com::Support::PARTIAL:    color = Color::YELLOW; break;
            default:                       color = Color::RED;    break;
        }*/
        controller.print(display);
    }

    if (paginationList.size() > pageSize)
    {
        paginationList.erase(paginationList.begin(),
            paginationList.begin() + static_cast<ptrdiff_t>(pageSize));
        paginationList.shrink_to_fit();
        controller.print("\r\n  --More--");
        controller.flush();
    }
    else
    {
        paginationList.clear();
        controller.print("\r\n");
        handlePrompt();
    }
    return true;
}

} // namespace cli
