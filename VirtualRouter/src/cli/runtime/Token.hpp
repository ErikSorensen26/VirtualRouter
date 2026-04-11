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

/**
 * @brief Pattern category assigned to a token matched against a variable placeholder.
 *
 * Set on a @ref Token when the parser resolves a user-typed value against a
 * named placeholder in the command tree (e.g. `A.B.C.D`, `LINE`, `<0-255>`).
 * `None` means the token is a fixed command keyword, not a user-supplied value.
 */

enum Pattern : uint64_t
{
    P_ARG,           ///< Does not match anything, also represents all.
    P_WORD,          ///< Matches the `WORD` placeholder (hostname, name, identifier).
    P_LINE,          ///< Matches the `LINE` placeholder; value contains the full remainder of the input.
    P_IPV4,          ///< Matches the `A.B.C.D` placeholder.
    P_IPV6,          ///< Matches the `X:X:X:X::X` placeholder.
    P_IPV6PFX,       ///< Matches the `X:X:X:X::X/<0-128>` placeholder.
    P_IPV4PFX,       ///< Matches the `A.B.C.D/nn` placeholder.
    P_MAC,           ///< Matches the `H.H.H` placeholder.
    P_NUMRNG,        ///< Matches a `<lo-hi>` numeric range placeholder.
    P_NONE,          ///< Fixed command keyword — not a user-supplied variable.
    P_COUNT,
};


/**
 * @brief Maps a placeholder string from the command tree to its @ref Pattern.
 *
 * Called during token resolution to classify whether a command-tree node is a
 * user-supplied variable placeholder (e.g. `"A.B.C.D"`, `"LINE"`, `"<0-255>"`)
 * or a fixed keyword. Returns @ref Pattern::NONE for anything that does not
 * match a known placeholder.
 *
 * @param p  Placeholder string exactly as it appears in the command tree.
 * @return   The @ref Pattern that matches @p p, or `Pattern::NONE`.
 */
static Pattern matchVolatilePattern(std::string_view p)
{
    if (p == "LINE")               return P_LINE;
    if (p == "WORD")               return P_WORD;
    if (p == "A.B.C.D")            return P_IPV4;
    if (p == "X:X:X:X::X")         return P_IPV6;
    if (p == "X:X:X:X::X/<0-128>") return P_IPV6PFX;
    if (p == "A.B.C.D/nn")         return P_IPV4PFX;
    if (p == "H.H.H")              return P_MAC;
    if (utils::isNumericRange(p))  return P_NUMRNG;
    return P_NONE;
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
        : hash(tokenHash(token)), value(token), pattern(P_NONE)
    {}

    static constexpr uint64_t tokenHash(std::string_view sv)
    {
        uint64_t h = 0;
        for (char c : sv) {
            h = h * 31 + static_cast<uint64_t>(c);
        }
        if (h <= static_cast<uint64_t>(P_COUNT))
            h += static_cast<uint64_t>(P_COUNT);
        return h;
    }

    uint64_t hash;
    std::string_view value;
    Pattern pattern = P_NONE;

    bool isLine()    const { return pattern == P_LINE; }
    bool isPattern() const { return pattern != P_NONE; }

    operator std::string_view() const { return value; }
    operator uint64_t() const { return hash; }
};

consteval uint64_t operator""_tok(const char* str, size_t len)
{
    return Token::tokenHash(std::string_view(str, len));
}

inline bool operator==(const Token& t, std::string_view s) { return t.value == s; }
inline bool operator!=(const Token& t, std::string_view s) { return t.value != s; }

inline bool operator==(const Token& t, uint64_t s) { return t.hash == s; }
inline bool operator==(uint64_t s, const Token& t) { return t.hash == s; }
inline bool operator!=(const Token& t, uint64_t s) { return t.hash != s; }
inline bool operator!=(uint64_t s, const Token& t) { return t.hash != s; }

struct TokenPtr
{
    TokenPtr(Token* p = nullptr)
        : ptr(p)
    {}

    operator Token*() const { return ptr; }
    explicit operator bool() const { return ptr != nullptr; }
    Token* ptr = nullptr;
};

struct TokenSegment
{
    TokenSegment(const std::span<Token>* s = nullptr)
        : ptr(s)
    {}

    operator const std::span<Token>*() const { return ptr; }
    explicit operator bool() const { return ptr != nullptr; }
    const std::span<Token>* ptr = nullptr;

    inline TokenPtr operator>>(size_t idx)
    {
        if (!ptr) return nullptr;
        if (idx >= ptr->size()) return nullptr;
        return &(*ptr)[idx];
    }
};

inline TokenPtr operator>>(const std::span<Token>& seg, size_t idx)
{
    if (idx >= seg.size()) return nullptr;
    return &seg[idx];
}

inline TokenSegment operator>>(const std::vector<std::span<Token>>& segs, size_t idx)
{
    if (idx >= segs.size()) return TokenSegment{};
    return TokenSegment{&segs[idx]};
}

inline bool operator==(const TokenPtr& t, uint64_t sv)
{
    if (!t.ptr) return false;
    return sv == t.ptr->hash;
}

inline bool operator!=(const TokenPtr& t, uint64_t sv)
{
    if (!t.ptr) return true;
    return sv != t.ptr->hash;
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
inline std::vector<std::span<Token>> segmentTokens(std::span<Token> tokens, size_t threshold)
{
    std::vector<std::span<Token>> segments;
    const size_t n = tokens.size();
    if (n == 0) return segments;

    size_t firstPattern = n;
    for (size_t j = 0; j < n; ++j)
    {
        if (tokens[j].isPattern())
        {
            firstPattern = j;
            break;
        }
    }

    size_t literalBeforePattern = n;
    if (firstPattern > 0)
        literalBeforePattern = firstPattern - 1;

    size_t i = std::min(threshold < n ? threshold : n, literalBeforePattern);

    while (i < n)
    {
        size_t segmentStart = i;
        if (tokens[segmentStart].isPattern())
        {
            ++segmentStart;
            if (segmentStart >= n) break;
        }

        size_t j = segmentStart + 1;
        while (j < n && tokens[j].isPattern())
            ++j;

        segments.emplace_back(tokens.subspan(segmentStart, j - segmentStart));
        i = j;
    }
    return segments;
}
}


#endif // TOKEN_HPP
