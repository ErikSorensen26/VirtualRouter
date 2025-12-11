// CliModeParser.hpp

#ifndef CLI_MODE_PARSER_HPP
#define CLI_MODE_PARSER_HPP

#include <string>
#include <vector>
#include <type_traits>
#include <Command.hpp>

#define UNUSED(x) (void)(x)

namespace Cli
{
// User-Defined literals to make tokens nicer
template <FixedString S>
consteval auto operator""_tok()
{
    return S;
}

// CliEngine: Executes commands for a given Context + command set
template <typename Context, typename... Commands>
class CliModeParser
{
    static_assert((std::is_same_v<Context, typename Commands::ContextType> && ...),
                  "All Commands must share the same Context type");

public:
    using ContextType = Context;
    
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
            if (!executed && CmdT::tryExecute(ctx, first, last))
                executed = true;
        };

        (tryOne(Commands{}), ...);

        return executed;
    }
};
}

#endif // CLI_MODE_PARSER_HPP
