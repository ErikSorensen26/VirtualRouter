// CliSession.cpp

#include <algorithm>
#include <regex>
#include <sstream>

#include <Global.h>

#include "Token.hpp"
#include "CliSession.h"
#include "CliEngine.h"
#include "CliUtils.h"
#include "cli/modes/Mode.hpp"

namespace cli
{
static std::string lowerStr(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return static_cast<unsigned char>(std::tolower(c)); });
    return s;
}

static bool lowerCmp(std::string_view s1, std::string_view s2)
{
    if (s1.size() != s2.size())
        return false;
    return std::equal(s1.begin(), s1.end(), s2.begin(),
        [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
}

static bool partialLowerCmp(std::string_view partial, std::string_view s2)
{
    if (partial.size() > s2.size()) return false;
    return std::equal(partial.begin(), partial.end(), s2.begin(),
        [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
}

static std::string trimLeft(const std::string& s)
{
    auto it = std::find_if(s.begin(), s.end(),
        [](unsigned char c){ return !std::isspace(c); });
    return std::string(it, s.end());
}

static std::string getLastWord(const std::string& s)
{
    std::istringstream ss(s);
    std::string w, last;
    while (ss >> w) last = w;
    return last;
}

inline static bool isVolatile(std::string_view p)
{
    return matchVolatilePattern(p) != P_NONE;
}

struct NodeView
{
    const nlohmann::ordered_json* node;
    const nlohmann::ordered_json* overrideSubCommands = nullptr;

    const nlohmann::ordered_json* subCommands() const
    {
        if (overrideSubCommands)
            return overrideSubCommands;
        if (node && node->contains(CLI_JSON_SUBCOMMAND_ARRAY))
            return &(*node)[CLI_JSON_SUBCOMMAND_ARRAY];
        return nullptr;
    }
};

enum class MatchState
{
    NONE,
    PARTIAL,
    EXACT,
    PATTERN
};

enum class CommandState
{
    IN_PROGRESS,
    COMPLETE,
    INVALID,
    AMBIGUOUS
};

enum class InputMode
{
    NORMAL,
    HELP,
    TAB,
    LINE
};

struct ParseContext
{
    CliEngine& engine;
    const nlohmann::ordered_json* workingDirectory;

    bool negateMode = false;
    bool defaultMode = false;

    MatchState matchState = MatchState::NONE;
    CommandState commandState = CommandState::IN_PROGRESS;
    InputMode inputMode = InputMode::NORMAL;

    const nlohmann::ordered_json* currentDirectory = nullptr;
    NodeView* currentNodeView = nullptr;
    
    std::string_view previousMatch;
    std::string_view currentPattern;
    std::string_view endCmdStr;

    bool eoc = false; // End of Command
    bool err = false; // Error
    bool nwh = false; // Next Word Help

    std::vector<NodeView> tempDir;

    ParseContext(CliEngine& e, const nlohmann::ordered_json* wd)
        : engine(e), workingDirectory(wd)
    {
        currentDirectory = workingDirectory;
    }

    bool isRunning()         const { return commandState == CommandState::IN_PROGRESS; }
    bool isCommandValid()    const { return commandState == CommandState::COMPLETE; }
    bool isCommandInvalid()  const { return commandState == CommandState::INVALID; }
    bool isHelpActive()      const { return inputMode == InputMode::HELP || inputMode == InputMode::TAB; }
    bool isLineActive()      const { return inputMode == InputMode::LINE; }
    bool isMatchSuccessful() const { return matchState == MatchState::EXACT || matchState == MatchState::PATTERN; }
    bool isPatternMatching() const { return matchState == MatchState::PATTERN; }

    bool isValidDir(const nlohmann::ordered_json* dir) const
    {
        return dir && dir->is_object() && dir->contains(CLI_JSON_SUBCOMMAND_ARRAY);
    }

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

    std::vector<Com> availableAt(std::string_view userInput, std::vector<Com>& prevCommands)
    {
        Com* prevCmd = prevCommands.empty() ? nullptr : &prevCommands.front();

        if (err) return {};

        bool addCarriage = false;
        try
        {
            if (negateMode || defaultMode)
                addCarriage = hasProp(prevCmd, "negate", "negate_all");
        }
        catch (...) { return { engine.carriageReturnCommand }; }

        std::vector<NodeView> views;
        buildViews(views);

        std::vector<Com> available;
        const nlohmann::ordered_json* matchNode = nullptr;
        Com exactMatch;
        bool hasExact = false;
        int matchCount = 0;

        for (const auto& view : views)
        {
            const auto* cmd = view.node;
            if (!cmd->contains(CLI_JSON_COMMAND_NAME) || !cmd->contains(CLI_JSON_DESCRIPTION)) continue;
            if (shouldHide(*cmd, negateMode || defaultMode)) continue;

            Com c = buildCom(*cmd, userInput);

            if (!isVolatile(c.name) && c.name.size() >= userInput.size())
            {
                if (c.isPartial())
                    matchNode = cmd, ++matchCount;
                if (c.isExact())
                    hasExact = true, exactMatch = c, exactMatch.name = c.name, matchNode = cmd;
            }
            available.push_back(c);
        }

        if (!matchCount && !hasExact)
        {
            for (const auto& v : views)
            {
                const std::string& name = (*v.node)[CLI_JSON_COMMAND_NAME].get<std::string>();
                if (matchPattern(userInput, name) && !eoc)
                {
                    matchNode = v.node;
                    matchCount = 1;
                    if (!isValidDir(matchNode)) eoc = true;
                    break;
                }
            }
        }

        if (addCarriage) available.push_back(engine.carriageReturnCommand);
        if (hasExact) matchCount = 1;

        if (matchCount == 1 && isValidDir(matchNode))
        {
            currentDirectory = &(*matchNode)[CLI_JSON_SUBCOMMAND_ARRAY];
            commandState = CommandState::IN_PROGRESS;

            for (const auto& sub : *currentDirectory)
                if (sub[CLI_JSON_COMMAND_NAME] == engine.carriageReturnCommand.name)
                    commandState = CommandState::COMPLETE;

            if (hasExact) return { exactMatch };
        }
        else if (!isMatchSuccessful() && (matchCount != 1 || isValidDir(matchNode)))
            err = true;
        else if (hasExact && !isValidDir(matchNode) && !userInput.empty() &&
                 !((negateMode && userInput == "no") || (defaultMode && userInput == "default")))
        {
            endCmdStr = (*matchNode)[CLI_JSON_COMMAND_NAME].get_ref<const std::string&>();
            eoc = true;
            return {};
        }
        else eoc = false;

        if (!matchCount && !userInput.empty() && err && userInput != "?" && userInput != "\t")
            return {};
        if (!matchCount && !isValidDir(matchNode) && userInput != "?" && userInput != "\t" && !isPatternMatching())
            return {};
        return available;
    }

    bool matchPattern(std::string_view input, const std::string& pattern)
    {
        if (previousMatch.empty() || input == "?" || input == "\t")
            return false;

        auto accept = [&](bool lineMode = false, bool endMode = false)
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

        // Numeric range
        if (cli::utils::matchNumericRange(input, pattern))
        {
            accept(false, true);
            return true;
        }

        return false;
    }

private:

    // AVAILABLE AT HELPERS

    static bool hasProp(const Com* cmd, const std::string& a, const std::string& aAll)
    {
        if (!cmd) return false;
        for (const auto& p : cmd->properties)
        {
            if (p == a) return true;
            if (p == aAll) throw true;
        }
        return false;
    }

    static bool shouldHide(const nlohmann::ordered_json& cmd, bool negate)
    {
        if (!cmd.contains(CLI_JSON_COMMAND_PROPERTIES)) return false;

        for (const auto& p : cmd[CLI_JSON_COMMAND_PROPERTIES])
        {
            const std::string ps = p.get<std::string>();
            if ((negate && ps == "negate_hide") ||
                (!negate && ps == "negate_show"))
                return true;
        }
        return false;
    }

    static Com buildCom(const nlohmann::ordered_json& cmd, std::string_view raw)
    {
        Com c;
        c.name = cmd[CLI_JSON_COMMAND_NAME].get_ref<const std::string&>();
        c.description = cmd[CLI_JSON_DESCRIPTION].get_ref<const std::string&>();
        // c.support TODO: lookup in executor
        if (cmd.contains(CLI_JSON_COMMAND_PROPERTIES))
            for (const auto& p : cmd[CLI_JSON_COMMAND_PROPERTIES])
                c.properties.push_back(p.get_ref<const std::string&>());
        
        if (lowerCmp(raw, c.name))
            c.match = Com::Match::FULL;
        else if (partialLowerCmp(raw, c.name))
            c.match = Com::Match::PARTIAL;

        return c;
    }

    void buildViews(std::vector<NodeView>& views)
    {
        for (const auto& cmd : *currentDirectory)
        {
            std::string_view name = cmd[CLI_JSON_COMMAND_NAME].get_ref<const std::string&>();

            // recursive
            if (cmd.contains(CLI_JSON_COMMAND_PROPERTIES))
            {
                for (const auto& p : cmd[CLI_JSON_COMMAND_PROPERTIES])
                    if (p.get<std::string>() == "recursive")
                    {
                        tempDir.push_back({ &cmd, currentDirectory });
                        views.push_back(tempDir.back());
                        goto next;
                    }
            }

            // variable
            if (name != engine.carriageReturnCommand.name &&
                !isVolatile(name) && name.size() >= 2 &&
                name.front() == '<' && name.back() == '>')
            {
                auto* sub = cmd.contains(CLI_JSON_SUBCOMMAND_ARRAY)
                          ? &cmd[CLI_JSON_SUBCOMMAND_ARRAY] : nullptr;

                std::string_view inner = name.substr(1, name.size() - 2);

                if (sub && engine.getCommandTree()[VARIABLE_OBJ].contains(inner))
                {
                    for (const auto& var : engine.getCommandTree()[VARIABLE_OBJ][inner])
                    {
                        tempDir.push_back({ &var, sub });
                        views.push_back(tempDir.back());
                    }
                }
                std::cout << engine.getCommandTree()[VARIABLE_OBJ].dump(4) << std::endl;
                continue;
            }

            views.push_back({ &cmd, nullptr });

            next:;
        }
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

CliSession::CliSession(CliEngine& engine, ConsoleController& controller, bool enableDebug)
    : Console(controller), engine(engine), execution(*this)
{
    configNode = &engine.getCommandTree();
    modeHistory.push_back(configNode);
    changeMode<CliMode::UserExec>(engine.global.configs);
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
        if (!resetAndChangeMode<CliMode::PrivilegedExec>(engine.global.configs))
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
    ParseContext ctx(engine, workingDirectory);

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

    if (!changeMode<CliMode::PrivilegedExec>(engine.global.configs))
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

    changeMode<CliMode::GlobalConfiguration>(engine.global.configs);
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

} // namespace cli
