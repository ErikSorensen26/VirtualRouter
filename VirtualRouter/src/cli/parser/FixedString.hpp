// FixedString.hpp

#ifndef FIXED_STRING_HPP
#define FIXED_STRING_HPP

#include <cstddef>
#include <string_view>

namespace Cli
{
template <std::size_t N>
struct FixedString
{
    char value[N];

    constexpr FixedString(const char (&str)[N])
    {
        for (std::size_t i = 0; i < N; ++i)
            value[i] = str[i];
    }
    constexpr std::string_view view() const
    {
        return std::string_view{value, N - 1};
    }
};
}

#endif // FIXED_STRING_HPP
