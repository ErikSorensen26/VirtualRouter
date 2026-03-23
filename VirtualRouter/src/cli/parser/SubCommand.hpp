// SubCommand.hpp

#ifndef SUB_COMMAND_HPP
#define SUB_COMMAND_HPP

#include <type_traits>
#include <vector>
#include <string>

#include "Command.hpp"
#include "FixedString.hpp"

namespace cli
{
template <typename T>
inline constexpr bool is_arg_tag_v =
    std::is_same_v<std::decay<T>, ArgTag> || std::is_same_v<std::decay<T>, ArgRestTag>;

template <
    typename Context,
    typename SubParser,
    FixedString... PrefixParts
>
struct SubCommand
{
    using ContextType = Context;

    static_assert(
        std::is_same_v<typename SubParser::ContextType, Context>,
        "SubCommand: SubParser must use the same Context type"
    );

    // Disallow ARG / ARG_REST in the prefix
    static_assert(
        (!is_arg_tag_v<decltype(PrefixParts)> && ...),
        "SubCommand: prefix cannot include ARG or ARG_REST; use only fixed tokens"
    );

    template <typename It>
    static bool match(It first, It last)
    {
        auto it = first;

        bool ok = true;
        auto matchPrefixOne = [&](auto part)
        {
            if (!ok) return;

            if (it == last) { ok = false; return; }

            const std::string_view sv = part.view();
            if (cli::as_sv(*it) != sv) { ok = false; return; }

            ++it;
        };

        (matchPrefixOne(PrefixParts), ...);

        if (!ok)
            return false;

        if (it == last)
            return true;

        return SubParser::match(it, last);
    }

    static bool match(const std::vector<std::string>& tokens)
    {
        return match(tokens.begin(), tokens.end());
    }

    template <typename It>
    static bool tryExecute(Context& ctx, It first, It last)
    {
        auto it = first;

        bool ok = true;
        auto matchPrefixOne = [&](auto part)
        {
            if (!ok) return;

            if (it == last) { ok = false; return; }

            const std::string_view sv = part.view();
            if (cli::as_sv(*it) != sv) { ok = false; return; }

            ++it;
        };

        (matchPrefixOne(PrefixParts), ...);

        if (!ok)
            return false;

        return SubParser::execute(ctx, it, last);
    }

    static bool tryExecute(Context& ctx, const std::vector<std::string>& tokens)
    {
        return tryExecute(ctx, tokens.begin(), tokens.end());
    }
};

template <
    typename Context,
    typename SubParser,
    auto... Parts
>
using subAdder = SubCommand<Context, SubParser, Parts...>;
}

#endif // SUB_COMMAND_HPP
