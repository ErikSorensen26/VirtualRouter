// Token.hpp

#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <cstdint>
#include <string_view>
#include "cli/tree/nodes/Command.h"

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
 * @ref P_NONE means the token is a fixed command keyword, not a user-supplied
 * value.
 */
enum Pattern : uint8_t
{
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
 * or a fixed keyword. Returns @ref P_NONE for anything that does not match a
 * known placeholder.
 *
 * @param p  Placeholder string exactly as it appears in the command tree.
 * @return   The @ref Pattern that matches @p p, or @ref P_NONE.
 */
inline Pattern matchVolatilePattern(std::string_view p)
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
 * from the command tree (always lower-case); for pattern tokens it is the raw
 * user input preserved as typed (e.g. an IP address or free-form text).
 *
 * `node` is the index of the command-tree node the word resolved to, which is
 * how a token says *which* command it is rather than merely what it spelled.
 * For a pattern token that is the placeholder node the value matched, not the
 * value itself, so the config binding is reachable from the token that carries
 * the argument. Words the parser could not resolve -- an unknown command, an
 * ambiguous prefix, the synthesized end-of-command marker -- carry
 * @ref NO_NODE, so anything reading it must check @ref hasNode first.
 *
 * Only the index is kept, not a whole @ref tree::Command: every token on a line
 * is resolved against the same tree, so the tree pointer is the caller's to
 * supply and storing it once per token would be dead weight.
 *
 * LINE tokens are collapsed — all words that follow the LINE placeholder are
 * joined into the same token's `value` with spaces, so command handlers receive
 * the entire free-text argument as a single string.
 */
struct Token
{
    Token(std::string_view token, tree::Command& n)
        : value(token), node(n), pattern(matchVolatilePattern(n.name()))
    {}

    /// @brief A word no node resolved: an unknown command, an ambiguous prefix,
    /// or the synthesized end-of-command marker. The cursor stays invalid, which
    /// is what hasNode() and the flag accessors below test for.
    explicit Token(std::string_view token)
        : value(token)
    {}

    std::string_view  value;
    tree::Command node;
    Pattern           pattern = P_NONE;

    bool isLine()     const { return pattern == P_LINE; }

    // node() resolves against the tree without checking it is there, so an
    // unresolved token has to be turned away here rather than inside it.
    bool hasNode()    const { return node.valid() && node.node().hasConfig(); }
    bool modeChange() const { return node.valid() && node.node().hasModeChange(); }
    bool tupChange()  const { return node.valid() && node.node().hasTuple(); }
    bool enumChange() const { return node.valid() && node.node().hasEnumChange(); }
    bool deferred()   const { return node.valid() && node.node().hasDeferred(); }
    bool resolver()   const { return node.valid() && node.node().hasResolver(); }

    /// @brief An enum member that sets one bit of a bitmap field, not a value.
    bool bitMapFlag() const { return enumChange() && node.node().hasEnumBitMap(); }

    /** @brief The REGISTRY_CHANGE flag, without requiring a bound field.
     *
     * Grouped like modeFlagged() and for the same reason: a rescope takes a
     * key -- `ip dhcp pool LAN` -- and the key token carries no config of its
     * own, so the group must claim it before any binding is resolved.
     */
    bool registryFlagged() const
    {
        return node.valid() && node.node().has(tree::CommandNode::REGISTRY_CHANGE);
    }

    /** @brief The MODE_CHANGE flag alone, without requiring a bound field.
     * 
     * Grouping asks this rather than modeChange(): a line entering a mode is
     * one command, and its key tokens carry no config of their own, so the
     * group has to be able to claim them before any binding is looked at.
     */
    bool modeFlagged() const
    {
        return node.valid() && node.node().has(tree::CommandNode::MODE_CHANGE);
    }

    /**
     * @brief True for `exit` and `end`, which leave a mode rather than enter one.
    
     * Unlike modeChange() this does not want a bound field: an exit returns to
     * whatever the navigation stack held, so it names no registry.
     */
    bool modeExit() const { return node.valid() && node.node().hasModeExit(); }

    operator std::string_view() const { return value; }
};

inline bool operator==(const Token& t, std::string_view s) { return t.value == s; }
inline bool operator!=(const Token& t, std::string_view s) { return t.value != s; }
}

#endif // TOKEN_HPP
