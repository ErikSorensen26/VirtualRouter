// CliSession.cpp

#include <algorithm>
#include <regex>
#include <sstream>

#include <Global.h>
#include "configs/registry/global/GlobalRegistry.h"

#include "cli/grammar/Token.hpp"
#include "CliSession.h"
#include "CliEngine.h"
#include "CliUtils.h"
#include "CommandTree.h"
#include "cli/modes/Mode.hpp"

namespace cli
{
using cli::utils::getLastWord;
using cli::utils::lowerCmp;
using cli::utils::lowerStr;
using cli::utils::partialLowerCmp;
using cli::utils::trimLeft;

CliSession::CliSession(CliEngine& engine, ConsoleController& controller, bool enableDebug)
    : Console(controller), engine(engine), execution(*this)
{
    configNode = &engine.getCommandTree();
    modeHistory.push_back(configNode);
    changeMode<CliMode::UserExec>(engine.global.getConfigs());
    initConsole();

#ifdef DEBUG
    isDebugModeEnabled = enableDebug;
#endif

    controller.print("Initializing Terminal...\r\n");
}

void CliSession::handlePrompt()
{
    setPrompt(engine.global.getHostname() + currentPrompt);

    insert = false;
    insertString.clear();
    cursorPos = 0;

    std::string preload;
    if (!nextLine.empty())
    {
        preload = nextLine;
        cursorPos      = preload.size();
        oldInputLength = preload.size();
        controller.print(preload);
        nextLine.clear();
    }
    inputCacheBuffer = preload;
}

bool CliSession::handleInput(std::string test)
{
    std::string userCommand = input(test, !paginationList.empty());

    if (userCommand == "CRT-Z" && getMode() != CliMode::UserExec)
    {
        if (!resetAndChangeMode<CliMode::PrivilegedExec>(engine.global.getConfigs()))
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
    ParseContext ctx(engine.getCommandTreeRef(), engine.carriageReturnCommand, workingDirectory);

    std::vector<std::string_view> words = ctx.tokenize(rawInput);
    if (words.empty()) return result;

    // Detect help or tab taken
    const bool hasHelpToken = words.back() == "?" || words.back() == "\t";

    // Handle "no" and "default"
    auto handlePrefix = [&](const std::string& prefix, bool& flag)
    {
        if (!lowerCmp(words[0], prefix) || words.size() < 2) return;
        const CliMode m = execution.getMode();
        if (m != CliMode::UserExec && m != CliMode::PrivilegedExec)
        {
            flag = true;
        }
    };

    handlePrefix("no", result.negate);
    handlePrefix("default", result.defaulted);

    // Handle "do-exec"
    if (!hasHelpToken && words.size() >= 2 && lowerStr(std::string(words[0])) == "do-exec")
    {
        const size_t pos = rawInput.find_first_not_of(" \t", 7);
        result.status = ParseResult::Status::DO_COMMAND;
        result.doRemainder = (pos != std::string::npos) ? rawInput.substr(pos) : "";
        return result;
    }

    // if (words[0] == "?" || words[0] == "\t") ctx.matchState = MatchState::EXACT;

    std::vector<Token> tokens;
    std::vector<Com> prevCommands;

    for (size_t idx = 0; idx < words.size(); idx++)
    {
        std::string_view word = words[idx];
        
        // Extend line token if active
        /*if (tokens.back().isLine())
        {
            ctx.extendLineToken(rawInput, word, hasHelpToken);
        }*/

        ctx.matchState = MatchState::NONE;
        std::vector<Com> available = ctx.availableAt(word, prevCommands);

        // Help
        if (word == "?")
        {
            prevCommands.swap(available);
            result.nextLine = rawInput.substr(0, rawInput.size() - 1);
            if (prevCommands.empty())
                controller.print("\r\n% Unrecognized Command");
            result.helpList = ctx.nwh ? available : prevCommands;
            result.status = ParseResult::Status::HELP;
            return result;
        }

        // Tab
        if (word == "\t")
        {
            if (ctx.nwh || prevCommands.size() != 1)
            {
                result.nextLine = rawInput.substr(0, rawInput.size() - 1);
                result.status = ParseResult::Status::TAB;
                return result;
            }

            // Single match autocomplete
            std::string_view stripped = rawInput;
            if (!stripped.empty()) stripped.remove_suffix(1);
            long lastSp = static_cast<long>(stripped.rfind(' '));
            std::string nl;
            if (lastSp < 0)
                nl = std::string(prevCommands[0].name) + " ";
            else
                nl = std::string(stripped.substr(0, static_cast<size_t>(lastSp + 1))) + std::string(prevCommands[0].name) + " ";
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
        std::vector<Com> matches;
        for (const auto& cmd : available)
        {
            if (ctx.isPatternMatching() && cmd.name == ctx.currentPattern)
                matches.push_back(cmd);
            else if (cmd.name.size() >= word.size() && partialLowerCmp(word, cmd.name))
                matches.push_back(cmd);
        }

        prevCommands = matches.empty() ? available : matches;

        // Match quality
        if (prevCommands.size() == 1 && !matches.empty())
        {
            if (ctx.isPatternMatching() && ctx.currentPattern == matches[0].name)
                ctx.matchState = MatchState::PATTERN;
            else if (matches[0].isExact())
                ctx.matchState = MatchState::EXACT;
            else
                ctx.matchState = MatchState::PARTIAL;
        }
    
        // Append token
        if (matches.size() <= 1)
        {
            if (ctx.isPatternMatching() && !matches.empty())
            {
                tokens.emplace_back(word, ctx.currentPattern);
                ctx.previousMatch = matches[0].name;
            }
            else if (matches.size() == 1)
            {
                tokens.emplace_back(matches[0].name);
                ctx.previousMatch = matches[0].name;
            }
            else if (matches.empty() && ctx.eoc)
            {
                tokens.emplace_back(ctx.endCmdStr);
            }
            else if (matches.empty())
            {
                tokens.clear();
                tokens.emplace_back(word);
            }
            else
            {
                tokens.emplace_back(matches[0].name);
                ctx.previousMatch = matches[0].name;
            }
        }
        else
        {
            tokens.emplace_back(word);
        }

        if (idx == 0)
        {
            if (lowerCmp(word, "no") && result.negate) ctx.currentDirectory = ctx.workingDirectory;
            if (lowerCmp(word, "default") && result.defaulted) ctx.currentDirectory = ctx.workingDirectory;
        }
    }

    if (ctx.eoc) ctx.commandState = CommandState::COMPLETE;

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

bool CliSession::executeCommand(std::string& command)
{
    isModeChanged = false;
    textLine      = false;
    execution.getContext().negate   = false;
    execution.getContext().defaulted = false;

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
            const std::string marker =
                "\r\n"
                + std::string(initialLineLength + parsed.markerCommand.size(), ' ')
                + "^\r\n% Invalid input detected at '^' marker.\r\n";
            controller.print(marker);
            return false;
        }

        case ParseResult::Status::AMBIGUOUS:
            controller.print("\r\n% Ambiguous command: \"" + parsed.ambiguousToken + "\"");
            return false;

        case ParseResult::Status::INCOMPLETE:
            if (tryGlobalCommand(command)) return true;
            controller.print("\r\n% Incomplete Command");
            return false;

        case ParseResult::Status::GLOBLA_CMD:
            return true;

        case ParseResult::Status::DO_COMMAND:
            tryDoCommand(parsed.doRemainder);
            return false;

        case ParseResult::Status::OK_:
            break;
    }

    // Apply negate/default flags to the context
    if (parsed.negate)
        execution.getContext().negate = true;
    if (parsed.defaulted)
        execution.getContext().defaulted = true;

    // Set textLine from token patterns (LINE already collapsed in parseInput)
    textLine = std::any_of(parsed.tokens.begin(), parsed.tokens.end(),
        [](const Token& t){ return t.isLine(); });

    // Build exec token list, skipping the leading "no"/"default" prefix
    std::span<Token> execTokens = (parsed.negate || parsed.defaulted)
        ? std::span<Token>(parsed.tokens.begin() + 1, parsed.tokens.size() - 1)
        : std::span<Token>(parsed.tokens);

    if (execTokens.empty()) return false;

    return executeModeParser(execTokens);
}

bool CliSession::popMode()
{
    if (navTop == 0) return false;

    const NavFrame& frame = navStack[--navTop];
    execution.restoreFromEntry(frame.executorEntry);
    currentPrompt    = frame.savedPrompt;
    workingDirectory = frame.savedWorkingDir;
    configNode       = frame.savedConfigNode;
    isModeChanged    = true;
    return true;
}

bool CliSession::tryDoCommand(const std::string& remainder)
{
    const std::string savedPrompt = currentPrompt;
    const json*       savedDir    = workingDirectory;
    const json*       savedCfg    = configNode;
    const size_t      savedNavTop = navTop;   // temp transition: undo nav push on return

    if (!changeMode<CliMode::PrivilegedExec>(engine.global.getConfigs()))
        return false;

    std::string cmd = remainder;
    const bool ok   = executeCommand(cmd);

    execution.revert();
    navTop           = savedNavTop;
    currentPrompt    = savedPrompt;
    workingDirectory = savedDir;
    configNode       = savedCfg;

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

    const std::string savedPrompt = currentPrompt;
    const json*       savedDir    = workingDirectory;
    const json*       savedCfg    = configNode;
    const size_t      savedNavTop = navTop;   // may revert if command fails

    changeMode<CliMode::GlobalConfiguration>(engine.global.getConfigs());
    historyToGlobal();

    std::string cmd = rawInput;
    const bool ok   = executeCommand(cmd);

    if (getMode() == CliMode::GlobalConfiguration && !ok)
    {
        execution.revert();
        navTop           = savedNavTop;
        currentPrompt    = savedPrompt;
        workingDirectory = savedDir;
        configNode       = savedCfg;
        return false;
    }

    return ok;
}

bool CliSession::setCommandDirectory(std::span<const std::string_view>& dir)
{
    auto& base = engine.getCommandTree();
    if (dir.empty() || !base.contains(dir[0]) || !base[dir[0]].is_array())
        return false;

    bool first = true;
    for (auto& step : dir)
    {
        if (first)
        {
            workingDirectory = &base[step];
            currentPrompt    = step;
            first            = false;
        }
        else
        {
            workingDirectory = &(*workingDirectory)[0][step];
        }
    }
    isModeChanged = true;

    if (!modeHistory.empty() && configNode != modeHistory.back())
        modeHistory.push_back(configNode);
    else
    {
        modeHistory.clear();
        modeHistory.push_back(&engine.getCommandTree());
    }
    return true;
}

CliMode CliSession::getMode()
{
    return execution.getMode();
}

void CliSession::historyToGlobal()
{
    prevConfig = configNode;
    modeHistory.clear();
    modeHistory.push_back(&engine.getCommandTree());
    configNode = &engine.getCommandTree();
}

void CliSession::displayCommands(std::vector<Com>& list)
{
    paginationList = list;
    maxNameLength  = 0;
    for (const auto& c : paginationList)
        if (c.name.size() > maxNameLength) maxNameLength = c.name.size();
}

bool CliSession::handlePagination(char nextch)
{
    if (nextch != '\0')
    {
        maxCommandLength = getTerminalWidth() - initialLineLength;

        if (nextch == '\x20')
        {
            controller.print("\033[2k\033[1G");
            controller.print("\033[1A");
        }
        else
        {
            controller.print("\033[2k\033[1G");
            controller.print(std::string(10, ' '));
            controller.print("\033[2k\033[1G");
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
        const Com& cmd = paginationList[i];
        std::string display = std::string("\r\n  ") + std::string(cmd.name);
        for (size_t j = 0; j <= (maxNameLength - cmd.name.size() + 5); ++j)
            display += ' ';

        const std::string_view desc = cmd.description;
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
            paginationList.begin() + static_cast<ptrdiff_t>(engine.paginationCount));
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

bool CliSession::executeModeParser(const std::span<Token> tokens)
{
    return execution.execute(tokens);
}

} // namespace cli
