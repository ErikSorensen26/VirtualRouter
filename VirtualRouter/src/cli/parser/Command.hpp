// Command.hpp

#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <string>
#include <vector>
#include <type_traits>
#include <cstddef>
#include <iterator>
#include <FixedString.hpp>

namespace Cli
{
struct ArgTag {};
inline constexpr ArgTag ARG{};

struct ArgRestTag {};
inline constexpr ArgRestTag ARG_REST{};

template <typename S>
static inline std::string_view as_sv(const S& s) noexcept
{
    return std::string_view{s.data(), s.size()};
}

template <
    typename Context,
    auto Handler,
    auto... Parts
>
struct Command
{
    using ContextType = Context;
    static constexpr auto handler = Handler;

    static constexpr size_t part_count = sizeof...(Parts);

    static constexpr bool has_rest_arg = 
        (std::is_same_v<std::decay_t<decltype(Parts)>, ArgRestTag> || ...);

    // Only one ARG_REST is allowed
    static_assert(
        ((std::is_same_v<std::decay_t<decltype(Parts)>, ArgRestTag> ? 1 : 0) + ...) <= 1,
        "Only one ARG_REST is allowed in the command pattern."
    );

    static_assert(
        std::is_invocable_v<decltype(Handler), Context&, const std::vector<std::string>&>,
        "Handler must be callable with (Context&, const std::vector<std::string>&)"
    );

    template <typename It>
    static bool match(It tokensFirst, It tokensLast) noexcept
    {
        const std::size_t tokenCount =
            static_cast<std::size_t>(std::distance(tokensFirst, tokensLast));

        if constexpr (!has_rest_arg)
        {
            if (tokenCount != part_count)
                return false;
        }
        else
        {
            if (tokenCount < part_count - 1)
                return false;
        }

        bool ok = true;
        size_t idx = 0;

        auto tokenAt = [&](size_t i) -> std::string_view
        {
            auto it = std::next(tokensFirst, static_cast<ptrdiff_t>(i));
            return as_sv(*it);
        };

        auto matchOne = [&](auto part)
        {
            if (!ok) return;

            using PartT = std::decay_t<decltype(part)>;

            if constexpr (std::is_same_v<PartT, ArgTag>)
            {
                if (idx >= tokenCount) { ok = false; return; }
                ++idx;
            }
            else if constexpr (std::is_same_v<PartT, ArgRestTag>)
            {
                idx = tokenCount;
            }
            else
            {
                const std::string_view sv = part.view();
                if (idx >= tokenCount || tokenAt(idx) != sv)
                {
                    ok = false;
                    return;
                }
                ++idx;
            }
        };

        (matchOne(Parts), ...);

        return ok;
    }

    static bool match(const std::vector<std::string>& tokens) noexcept
    {
        return match(tokens.begin(), tokens.end());
    }

    template <typename It>
    static bool tryExecute(Context& ctx, It tokensFirst, It tokensLast)
    {
        const std::size_t tokenCount =
            static_cast<std::size_t>(std::distance(tokensFirst, tokensLast));

        if constexpr (!has_rest_arg)
        {
            if (tokenCount != part_count)
                return false;
        }
        else
        {
            if (tokenCount < part_count - 1)
                return false;
        }

        std::vector<std::string> args;
        args.reserve(tokenCount);

        bool ok = true;
        std::size_t idx = 0;

        auto tokenAt = [&](std::size_t i) -> std::string_view
        {
            auto it = std::next(tokensFirst, static_cast<std::ptrdiff_t>(i));
            return as_sv(*it);
        };

        auto matchOne = [&](auto part)
        {
            if (!ok) return;

            using PartT = std::decay_t<decltype(part)>;

            if constexpr (std::is_same_v<PartT, ArgTag>)
            {
                if (idx >= tokenCount)
                {
                    ok = false;
                    return;
                }

                args.emplace_back(tokenAt(idx));
                ++idx;
            }
            else if constexpr (std::is_same_v<PartT, ArgRestTag>)
            {
                while (idx < tokenCount)
                {
                    args.emplace_back(tokenAt(idx));
                    ++idx;
                }
            }
            else
            {
                auto sv = part.view();
                if (idx >= tokenCount || tokenAt(idx) != sv)
                {
                    ok = false;
                    return;
                }
                ++idx;
            }
        };

        (matchOne(Parts), ...);

        if (!ok)
            return false;

        return Handler(ctx, args);
    }

    static bool tryExecute(Context& ctx, const std::vector<std::string>& tokens)
    {
        return tryExecute(ctx, tokens.begin(), tokens.end());
    }
};

template <
    typename Context,
    auto Handler,
    auto... Parts
>
using commandAdder = Command<Context, Handler, Parts...>;
}

#endif // COMMAND_HPP
