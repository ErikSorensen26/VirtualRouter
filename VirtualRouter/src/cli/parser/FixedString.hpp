/**
 * @file FixedString.hpp
 * @brief Compile-time string literal wrapper used as a non-type template parameter (NTTP).
 */

#ifndef FIXED_STRING_HPP
#define FIXED_STRING_HPP

#include <cstddef>
#include <string_view>

/**
 * @brief Compile-time CLI command parsing and dispatch.
 *
 * See Command.hpp for the full namespace description.
 */
namespace cli
{
/**
 * @brief Null-terminated string stored entirely at compile time.
 * @ingroup CLI_PARSER
 *
 * `FixedString` wraps a string literal so that it can appear as a non-type
 * template parameter (NTTP) in C++20. It is the primitive used by the
 * `operator""_tok` user-defined literal to embed fixed command tokens
 * directly in @ref Command and @ref SubCommand template argument lists.
 *
 * The array holds the full literal including the null terminator; `view()`
 * strips the trailing `\0` when producing a `std::string_view`.
 *
 * @tparam N Length of the string literal including the null terminator.
 *
 * @see Command
 * @see SubCommand
 */
template <std::size_t N>
struct FixedString
{
    char value[N]; ///< Raw character storage including null terminator.

    /**
     * @brief Constructs from a string literal.
     *
     * @param str The string literal to copy; must be exactly N characters long
     *            (enforced by the compiler's implicit template argument deduction).
     */
    constexpr FixedString(const char (&str)[N])
    {
        for (std::size_t i = 0; i < N; ++i)
            value[i] = str[i];
    }

    /**
     * @brief Returns a view over the string without the null terminator.
     */
    constexpr std::string_view view() const
    {
        return std::string_view{value, N - 1};
    }
};
}

#endif // FIXED_STRING_HPP
