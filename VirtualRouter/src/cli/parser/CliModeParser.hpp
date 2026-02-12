// CliModeParser.hpp

#ifndef CLI_MODE_PARSER_HPP
#define CLI_MODE_PARSER_HPP

#include <string>
#include <vector>
#include <type_traits>
#include <json.hpp>
#include <memory_resource>

#include "FixedString.hpp"
#include "cli/modes/Mode.hpp"

#define UNUSED(x) (void)(x)

namespace Cli
{
// User-Defined literals to make tokens nicer
template <FixedString S>
consteval auto operator""_tok()
{
    return S;
}

template <CliMode Mode, typename Context, typename... Commands>
class CliModeParser;

template <typename T, typename = void> struct is_cli_mode : std::false_type {};

template <typename T>
struct is_cli_mode<T, std::void_t<decltype(T::mode)>> : std::true_type {};

template <typename T>
inline constexpr bool is_cli_mode_v = is_cli_mode<std::decay_t<T>>::value;

// CliEngine: Executes commands for a given Context + command set
template <CliMode Mode, typename Context, typename... Commands>
class CliModeParser
{
    static_assert((std::is_same_v<Context, typename Commands::ContextType> && ...),
                  "All Commands must share the same Context type");

public:
    using ContextType = Context;
    static constexpr CliMode mode = Mode;
    
    static bool execute(Context& ctx, const std::vector<std::string>& tokens)
    {
        return execute(ctx, tokens.begin(), tokens.end());
    }

    template <typename It>
    static bool execute(Context& ctx, It first, It last)
    {
        bool executed = false;

        auto tryOne = [&](auto cmdType)
        {
            using CmdT = decltype(cmdType);

            if (executed)
                return;

            if constexpr (is_cli_mode_v<CmdT>)
            {
                if (CmdT::execute(ctx, first, last))
                    executed = true;
            }
            else
            {
                if (CmdT::tryExecute(ctx, first, last))
                    executed = true;
            }
        };

        (tryOne(Commands{}), ...);

        return executed;
    }

    static bool match(const std::vector<std::string>& tokens)
    {
        return match(tokens.begin(), tokens.end());
    }

    template <typename It>
    static bool match(It first, It last)
    {
        bool found = false;

        auto tryOne = [&](auto cmdType)
        {
            using CmdT = decltype(cmdType);

            if (found)
                return;

            if constexpr (is_cli_mode_v<CmdT>)
            {
                if (CmdT::match(first, last))
                    found = true;
            }
            else
            {
                if (CmdT::match(first, last))
                    found = true;
            }
        };

        (tryOne(Commands{}), ...);
        return found;
    }

    static void addSupport(nlohmann::json& base)
    {
        nlohmann::json* dir = &base;
        bool prompt = false;
        for (auto step : getPath(mode))
        {
            if (!prompt)
            {
                dir = &(*dir)[step];
                prompt = true;
            }
            else
                dir = &base[0][step];
        }

        std::pmr::unsynchronized_pool_resource pool;
        std::pmr::vector<std::pmr::string> tokList{&pool};
        tokList.reserve(256);

        for (auto& cmd : *dir)
        {
            supportTraverse(cmd, tokList, pool);
        }
    }
private:

    enum class Support { FULL, PARTIAL, NONE };
    static Support supportTraverse(nlohmann::json& cmd, std::pmr::vector<std::pmr::string>& toks, std::pmr::unsynchronized_pool_resource& pool)
    {
        if (!cmd.is_object() || !cmd.contains("name") || !cmd["name"].is_string() ||
            !cmd.contains("subcommands") || !cmd["subcommands"].is_array())
            return Support::NONE;
        toks.emplace_back(cmd["name"].get<std::string>(), &pool);
        bool isMatch = match(toks.begin(), toks.end());
        size_t fullMatches = 0;
        bool partial = false;
        for (auto& sub : cmd["subcommands"])
        {
            switch (supportTraverse(sub, toks, pool))
            {
                case Support::FULL:
                    fullMatches++;
                    break;
                case Support::PARTIAL:
                    partial = true;
                    break;
                case Support::NONE:
                    break;
            }
        }
        toks.pop_back();
        
        if (partial)
        {
            cmd["support"] = nlohmann::json::boolean_t(true);
            return Support::PARTIAL;
        }
        if (isMatch && fullMatches == cmd["subcommands"].size())
        {
            cmd["support"] = nlohmann::json::boolean_t(false);
            return Support::FULL;
        }

        cmd.erase("support");
        return Support::NONE;
    }
};
}

#endif // CLI_MODE_PARSER_HPP
