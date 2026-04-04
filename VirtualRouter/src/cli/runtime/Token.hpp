// Token.hpp

#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <span>
#include <vector>
#include <cstdint>
#include <string_view>

namespace cli
{
namespace utils
{
    bool isNumericRange(std::string_view);
}
constexpr uint64_t ARG = 0;


/**
 * @brief Pattern category assigned to a token matched against a variable placeholder.
 *
 * Set on a @ref Token when the parser resolves a user-typed value against a
 * named placeholder in the command tree (e.g. `A.B.C.D`, `LINE`, `<0-255>`).
 * `None` means the token is a fixed command keyword, not a user-supplied value.
 */
enum class PatternKind
{
    NONE,          ///< Fixed command keyword — not a user-supplied variable.
    WORD,          ///< Matches the `WORD` placeholder (hostname, name, identifier).
    LINE,          ///< Matches the `LINE` placeholder; value contains the full remainder of the input.
    IPV4,          ///< Matches the `A.B.C.D` placeholder.
    IPV6,          ///< Matches the `X:X:X:X::X` placeholder.
    IPV6_PREFIX,   ///< Matches the `X:X:X:X::X/<0-128>` placeholder.
    MAC,           ///< Matches the `H.H.H` placeholder.
    NUMERIC_RANGE, ///< Matches a `<lo-hi>` numeric range placeholder.
};

/**
 * @brief Maps a placeholder string from the command tree to its @ref PatternKind.
 *
 * Called during token resolution to classify whether a command-tree node is a
 * user-supplied variable placeholder (e.g. `"A.B.C.D"`, `"LINE"`, `"<0-255>"`)
 * or a fixed keyword. Returns @ref PatternKind::NONE for anything that does not
 * match a known placeholder.
 *
 * @param p  Placeholder string exactly as it appears in the command tree.
 * @return   The @ref PatternKind that matches @p p, or `PatternKind::NONE`.
 */
static PatternKind matchVolatilePattern(std::string_view p)
{
    if (p == "LINE")               return PatternKind::LINE;
    if (p == "WORD")               return PatternKind::WORD;
    if (p == "A.B.C.D")            return PatternKind::IPV4;
    if (p == "X:X:X:X::X")         return PatternKind::IPV6;
    if (p == "X:X:X:X::X/<0-128>") return PatternKind::IPV6_PREFIX;
    if (p == "H.H.H")              return PatternKind::MAC;
    if (utils::isNumericRange(p))  return PatternKind::NUMERIC_RANGE;
    return PatternKind::NONE;
}


/**
 * @brief A single resolved token produced by @ref CliSession::parseInput.
 *
 * `value` holds the canonical form: for keyword tokens it is the exact name
 * from the command tree (always lower-case); for pattern tokens it is the
 * raw user input preserved as typed (e.g. an IP address or free-form text).
 *
 * LINE tokens are collapsed — all words that follow the LINE placeholder are
 * joined into the same token's `value` with spaces, so command handlers
 * receive the entire free-text argument as a single string.
 */
struct Token
{
    Token(std::string_view token, std::string_view pattern)
        : hash(tokenHash(token)), value(token), pattern(matchVolatilePattern(pattern))
    {}

    Token(std::string_view token)
        : hash(tokenHash(token)), value(token), pattern(PatternKind::NONE)
    {}

    static constexpr uint64_t tokenHash(std::string_view sv)
    {
        uint64_t h = 0;
        for (char c : sv) {
            h = h * 31 + static_cast<uint64_t>(c);
        }
        if (h == 0) ++h;
        return h;
    }

    uint64_t hash;
    std::string_view value;
    PatternKind pattern = PatternKind::NONE;

    bool isLine()    const { return pattern == PatternKind::LINE; }
    bool isPattern() const { return pattern != PatternKind::NONE; }

    operator std::string_view() const { return value; }
    operator uint64_t() const { return hash; }
};

inline bool operator==(const Token& t, std::string_view s) { return t.value == s; }
inline bool operator!=(const Token& t, std::string_view s) { return t.value != s; }

inline bool operator==(const Token& t, uint64_t s) { return t.hash == s; }
inline bool operator==(uint64_t s, const Token& t) { return t.hash == s; }
inline bool operator!=(const Token& t, uint64_t s) { return t.hash != s; }
inline bool operator!=(uint64_t s, const Token& t) { return t.hash != s; }

inline Token* operator>>(const std::span<Token>& seg, size_t idx)
{
    if (idx >= seg.size()) return nullptr;
    return &seg[idx];
}


/**
 * @brief Splits a flat token span into keyword-anchored segments.
 *
 * Each segment begins at a keyword token (non-pattern) and extends to include
 * all immediately following pattern tokens. Leading pattern tokens (before the
 * first keyword) are discarded. This mirrors how Cisco-style CLI pipelines
 * separate sub-commands: the keyword is the command verb and any trailing
 * pattern tokens are its arguments.
 *
 * @code
 * // "ip address A.B.C.D A.B.C.D" produces two segments:
 * //   ["ip"]  and  ["address", <ipv4>, <ipv4>]
 * @endcode
 *
 * @param tokens  Flat span of resolved @ref Token objects from the parser.
 * @return        Vector of sub-spans, each starting at a keyword token.
 */
inline std::vector<std::span<Token>> segmentTokens(std::span<Token> tokens)
{
    std::vector<std::span<Token>> segments;
    const size_t n = tokens.size();
    size_t i = 0;

    while (i < n && tokens[i].isPattern())
        ++i;

    while (i < n)
    {
        size_t start = i;
        ++i;
        while (i < n && tokens[i].isPattern()) ++i;
        segments.emplace_back(tokens.data() + start, i - start);
    }

    return segments;
}
}


#endif // TOKEN_HPP
