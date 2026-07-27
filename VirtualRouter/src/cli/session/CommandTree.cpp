// CommandTree.cpp

#include <iostream>
#include <regex>
#include <unordered_map>

#include <FileSystem.hpp>
#include <hardware/HardwareManager.h>

#include "CommandTree.h"
#include "CliUtils.h"
#include "interface/configs/InterfaceType.hpp"

namespace cli
{
void CommandTree::load(FileSystem& fs)
{
    tree.clear();

    std::string fileStream;
    if (fs.fileExists(COMMAND_TREE_BIN) && fs.readFile(COMMAND_TREE_BIN, fileStream))
    {
        tree = nlohmann::ordered_json::from_cbor(
            reinterpret_cast<const uint8_t*>(fileStream.data()),
            reinterpret_cast<const uint8_t*>(fileStream.data()) + fileStream.size()
        );
    }
    else if (fs.fileExists(COMMAND_TREE) && fs.readFile(COMMAND_TREE, fileStream))
    {
        tree = nlohmann::ordered_json::parse(
            fileStream.data(),
            fileStream.data() + fileStream.size(),
            nullptr,
            false,
            true
        );

        std::vector<uint8_t> treeBin = nlohmann::ordered_json::to_cbor(tree);
        std::string binString(reinterpret_cast<const char*>(treeBin.data()), treeBin.size());
        fs.writeFile(COMMAND_TREE_BIN, binString);
    }
    else
    {
        std::cerr << "Failed to open command tree file: " << COMMAND_TREE << std::endl;
        tree = nlohmann::ordered_json::object();
    }
}

void CommandTree::initTree(hardware::HardwareManager& hwManager)
{
    if (tree.contains(VARIABLE_OBJ) && tree[VARIABLE_OBJ].contains("interface") && tree[VARIABLE_OBJ]["interface"].is_array())
    {
        nlohmann::ordered_json& vars = tree[VARIABLE_OBJ];

        for (const auto& [type, ifaces] : hwManager.getPhysicalInterfaces())
        {
            std::string typeStr = interface::getInterfaceType(type);
            if (vars.contains(typeStr) && vars[typeStr].is_array())
            {
                size_t size = ifaces.size();
                if (size != 0)
                    vars[typeStr][0][CLI_JSON_COMMAND_NAME] = "<0-" + std::to_string(size - 1) + ">";
                else
                    vars.erase(typeStr);
            }
        }
    }
}

bool CommandTree::isValidCommandDirectory(const nlohmann::ordered_json* directory)
{
    if (directory && directory->is_object())
    {
        return directory->contains(CLI_JSON_SUBCOMMAND_ARRAY);
    }
    return false;
}

// PARSE CONTEXT

std::vector<std::string_view> ParseContext::tokenize(const std::string& line)
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

void ParseContext::extendLineToken(const std::string& line, std::string_view& token, bool help)
{
    size_t offset = static_cast<size_t>(token.data() - line.data());
    if (help) offset++; // Increment to shave help command off the end
    if (offset > line.size()) return;
    token = std::string_view(line.data() + offset, line.size() - offset);
}

std::vector<Com> ParseContext::availableAt(std::string_view userInput, std::vector<Com>& prevCommands)
{
    Com* prevCmd = prevCommands.empty() ? nullptr : &prevCommands.front();

    if (err) return {};

    bool addCarriage = false;
    try
    {
        if (negateMode || defaultMode)
            addCarriage = hasProp(prevCmd, "negate", "negate_all");
    }
    catch (...) { return { carriageReturnCommand }; }

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

    if (addCarriage) available.push_back(carriageReturnCommand);
    if (hasExact) matchCount = 1;

    if (matchCount == 1 && isValidDir(matchNode))
    {
        currentDirectory = &(*matchNode)[CLI_JSON_SUBCOMMAND_ARRAY];
        commandState = CommandState::IN_PROGRESS;

        for (const auto& sub : *currentDirectory)
            if (sub[CLI_JSON_COMMAND_NAME] == carriageReturnCommand.name)
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

bool ParseContext::matchPattern(std::string_view input, const std::string& pattern)
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

// AVAILABLE AT HELPERS

bool ParseContext::hasProp(const Com* cmd, const std::string& a, const std::string& aAll)
{
    if (!cmd) return false;
    for (const auto& p : cmd->properties)
    {
        if (p == a) return true;
        if (p == aAll) throw true;
    }
    return false;
}

bool ParseContext::shouldHide(const nlohmann::ordered_json& cmd, bool negate)
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

Com ParseContext::buildCom(const nlohmann::ordered_json& cmd, std::string_view raw)
{
    Com c;
    c.name = cmd[CLI_JSON_COMMAND_NAME].get_ref<const std::string&>();
    c.description = cmd[CLI_JSON_DESCRIPTION].get_ref<const std::string&>();
    // c.support TODO: lookup in executor
    if (cmd.contains(CLI_JSON_COMMAND_PROPERTIES))
        for (const auto& p : cmd[CLI_JSON_COMMAND_PROPERTIES])
            c.properties.push_back(p.get_ref<const std::string&>());

    if (cli::utils::lowerCmp(raw, c.name))
        c.match = Com::Match::FULL;
    else if (cli::utils::partialLowerCmp(raw, c.name))
        c.match = Com::Match::PARTIAL;

    return c;
}

void ParseContext::buildViews(std::vector<NodeView>& views)
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
        if (name != carriageReturnCommand.name &&
            !isVolatile(name) && name.size() >= 2 &&
            name.front() == '<' && name.back() == '>')
        {
            auto* sub = cmd.contains(CLI_JSON_SUBCOMMAND_ARRAY)
                      ? &cmd[CLI_JSON_SUBCOMMAND_ARRAY] : nullptr;

            std::string_view inner = name.substr(1, name.size() - 2);

            if (sub && commandTree.root()[VARIABLE_OBJ].contains(inner))
            {
                for (const auto& var : commandTree.root()[VARIABLE_OBJ][inner])
                {
                    tempDir.push_back({ &var, sub });
                    views.push_back(tempDir.back());
                }
            }
            continue;
        }

        views.push_back({ &cmd, nullptr });

        next:;
    }
}

// MATCH PATTERN HELPERS

bool ParseContext::matchWord(std::string_view input, std::string_view previousMatch)
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
}
