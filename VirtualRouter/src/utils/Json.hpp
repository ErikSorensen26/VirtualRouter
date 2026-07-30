/**
 * @file Json.hpp
 * @brief A small recursive-descent JSON reader.
 *
 * Read-only and self-contained: text goes in, a @ref utils::json::JsonNode tree
 * comes out. There is no writer and no mutation API, because the two things this
 * parses — the CLI grammar and the hardware config — are inputs the router reads
 * at startup and never writes back.
 *
 * ## Ownership
 * A node owns its children by value and copies every string it parses, so the
 * tree outlives the text it came from. That costs a copy per string, which is
 * the right trade here: @ref utils::json::load reads a file into a temporary
 * buffer and returns the tree, and a borrowing parser would hand back views into
 * a buffer that had already gone. It is also why the grammar is flattened into
 * @ref cli::tree::CommandTree once and cached — a 25 MB document is expensive to
 * hold this way, so the tree form exists to avoid re-parsing it per startup.
 *
 * ## Objects keep their order
 * Object members are stored in a vector in document order, not a map. Lookup by
 * key is therefore linear, which is fine at these sizes and buys two things the
 * grammar depends on: members stay in the order the file lists them, and
 * duplicate keys are preserved rather than silently collapsing.
 *
 * ## Errors
 * Anything malformed throws @c std::runtime_error naming the byte offset. There
 * is no error-code path and no partially built tree: a document either parses
 * completely or the call throws.
 *
 * Known deviations from the spec, none of which either input document reaches:
 * - Only space, tab, and newline count as whitespace, not carriage return, so a
 *   CRLF-terminated document fails to parse.
 * - Surrogate pairs are only recombined when the high half is at most 0xD8FF,
 *   short of the real 0xDBFF bound, so a `\\u` pair above that is emitted as two
 *   separately encoded halves instead of one code point. Reachable only from
 *   U+90000 up.
 * - A high surrogate followed by a `\\u` escape that is not a low half consumes
 *   that second escape without emitting it, so its code point is lost.
 * - Unescaped control characters inside strings are accepted rather than
 *   rejected.
 */

#ifndef JSON_PARSER_HPP
#define JSON_PARSER_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <charconv>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <fstream>

namespace utils::json
{
/**
 * @brief One parsed JSON value, of any type.
 *
 * A single struct covers all six types rather than a variant or a class
 * hierarchy: @ref type says which of the value fields is meaningful, and the
 * rest sit unused. That trades memory per node for a uniform, copyable value
 * type with no indirection.
 *
 * Containers own their children directly, so a node is self-contained and
 * outlives the text it was parsed from.
 *
 * Accessors come in two kinds. The `is*` predicates only ever answer the
 * question. The `as*` accessors assert the type and throw on mismatch, so
 * reading a value the document did not contain fails loudly rather than
 * returning a default that silently propagates.
 */
struct JsonNode
{
    /// @brief Which JSON type this node holds, and so which value field is live.
    enum Type : uint8_t
    {
        OBJECT = 0, ///< Members in document order, each child carrying its @ref name.
        ARRAY  = 1, ///< Elements in order; children have no @ref name.
        STRING = 2, ///< Value in @ref strValue, escapes already decoded.
        NUMBER = 3, ///< Value in @ref numValue; JSON has one numeric type.
        BOOL   = 4, ///< Value in @ref boolValue.
        NILL   = 5  ///< JSON null. Also the type of a default-constructed node.
    };

    std::string name;               ///< Key this node was stored under; empty outside an object.
    Type type = NILL;               ///< Which of the value fields below is meaningful.
    std::string strValue;           ///< Decoded text; valid when @ref type is STRING.
    double numValue = 0.0;          ///< Numeric value; valid when @ref type is NUMBER.
    bool boolValue = false;         ///< Boolean value; valid when @ref type is BOOL.
    std::vector<JsonNode> children; ///< Members or elements; valid for OBJECT and ARRAY.

    /// @brief True when this node is a JSON object.
    bool isObject() const { return type == OBJECT; }
    /// @brief True when this node is a JSON array.
    bool isArray()  const { return type == ARRAY; }
    /// @brief True when this node is a JSON string.
    bool isString() const { return type == STRING; }
    /// @brief True when this node is a JSON number.
    bool isNumber() const { return type == NUMBER; }
    /// @brief True when this node is a JSON boolean.
    bool isBool()   const { return type == BOOL; }
    /// @brief True when this node is JSON null, or was never parsed into.
    bool isNull()   const { return type == NILL; }

    /**
     * @brief The string value.
     *
     * The view borrows from this node, so it stays valid only as long as the
     * node does and dangles if the node is moved from.
     *
     * @throws std::runtime_error when this node is not a string.
     */
    std::string_view asString() const
    {
        if (type != STRING) throw std::runtime_error("utils::json::JsonNode: not a string");
        return strValue;
    }

    /**
     * @brief The numeric value.
     *
     * JSON has a single numeric type, so integers arrive as doubles and are
     * exact only up to 2^53.
     *
     * @throws std::runtime_error when this node is not a number.
     */
    double asNumber() const
    {
        if (type != NUMBER) throw std::runtime_error("utils::json::JsonNode: not a number");
        return numValue;
    }

    /**
     * @brief The boolean value.
     *
     * @throws std::runtime_error when this node is not a bool.
     */
    bool asBool() const
    {
        if (type != BOOL) throw std::runtime_error("utils::json::JsonNode: not a bool");
        return boolValue;
    }

    /**
     * @brief Number of members or elements.
     *
     * Unlike the `as*` accessors this does not throw: a scalar simply has no
     * children, so 0 is the honest answer and lets a caller loop over a node
     * without first testing what it is.
     */
    size_t size() const
    {
        return (type == OBJECT || type == ARRAY) ? children.size() : 0;
    }

    /**
     * @brief The i-th child, by position.
     *
     * Works on objects as well as arrays, since object members keep document
     * order. That is what makes it possible to walk an object whose keys are
     * data rather than a fixed schema.
     *
     * @param i Zero-based ordinal, below @ref size.
     * @throws std::runtime_error when this node is not a container.
     * @throws std::out_of_range  when @p i is past the last child.
     */
    const JsonNode& at(size_t i) const
    {
        if (type != OBJECT && type != ARRAY)
            throw std::runtime_error("utils::json::JsonNode: indexing a non-container value");
        if (i >= children.size())
            throw std::out_of_range("utils::json::JsonNode: child ordinal out of range");
        return children[i];
    }

    /**
     * @brief Looks up a member by key, without throwing.
     *
     * The non-throwing counterpart to @ref operator[], for optional members: a
     * missing key and a non-object node both come back as null, so a caller can
     * probe one without knowing the shape of what it holds.
     *
     * Members are scanned linearly, which suits the small objects this parser is
     * used on but is not a good way to walk a large one key by key.
     *
     * If a document repeats a key, the first occurrence wins.
     *
     * @return The member, or nullptr. Valid only as long as this node is.
     */
    const JsonNode* find(std::string_view key) const
    {
        if (type != OBJECT) return nullptr;
        for (const JsonNode& c : children)
            if (c.name == key) return &c;
        return nullptr;
    }

    /// @brief True when this node is an object holding @p key.
    bool contains(std::string_view key) const { return find(key) != nullptr; }

    /**
     * @brief Looks up a required member by key.
     *
     * Throws where @ref find returns null, for members the caller believes the
     * document must have; the error names the key so a malformed config points
     * at itself.
     *
     * @throws std::out_of_range when this node is not an object, or has no @p key.
     */
    const JsonNode& operator[](std::string_view key) const
    {
        const JsonNode* c = find(key);
        if (!c)
            throw std::out_of_range("utils::json::JsonNode: no such member '" + std::string(key) + "'");
        return *c;
    }

    /**
     * @brief Iteration over children, in document order.
     *
     * Present so a node can be used directly in a range-for. On an array this
     * yields the elements; on an object, the members, each carrying its
     * @ref name. A scalar iterates zero times rather than failing.
     */
    std::vector<JsonNode>::const_iterator begin() const { return children.begin(); }
    /// @brief End of the child range. @see begin
    std::vector<JsonNode>::const_iterator end()   const { return children.end(); }
};

/**
 * @brief Recursive-descent parser turning JSON text into a @ref JsonNode tree.
 *
 * One instance parses one document: it holds a cursor into the text and is
 * consumed by the parse, so it is built, used once, and discarded. Callers
 * normally go through @ref parse or @ref load rather than naming this directly.
 *
 * The grammar is small enough that each production is one method and nesting is
 * carried by the C++ call stack. That keeps the code close to the JSON grammar,
 * at the cost of a stack depth proportional to document nesting; the documents
 * this parses are only a few levels deep.
 *
 * Errors are exceptions, never sentinel values, so a partial parse cannot be
 * mistaken for a successful one. Every failure routes through @ref fail and
 * reports the byte offset the parser had reached.
 *
 * The text is borrowed, not copied, and must outlive the parse. Parsed strings
 * are copied out into their nodes, so the resulting tree does not.
 */
class JsonParser
{
public:
    /**
     * @brief Binds a parser to the text to read.
     * @param text Borrowed for the lifetime of the parse; must outlive this parser.
     */
    explicit JsonParser(std::string_view text) :s(text) {}

    /**
     * @brief Parses the text as one complete JSON document.
     *
     * A document is a single value; anything after it other than whitespace is
     * an error, so a truncated or concatenated file is rejected rather than
     * silently parsed down to its first value.
     *
     * @return The root node, owning the whole tree.
     * @throws std::runtime_error on any malformed input, naming the byte offset.
     */
    JsonNode parseDocument()
    {
        skipWhitespace();
        JsonNode root = parseValue();
        skipWhitespace();
        if (pos != s.size()) fail("trailing data after JSON value");
        return root;
    }

private:
    std::string_view s; ///< Borrowed document text.
    size_t pos = 0;     ///< Read cursor, as a byte offset into @ref s.

    /**
     * @brief Aborts the parse with a message and the current offset.
     *
     * The single exit for every parse failure. Marked `[[noreturn]]` so callers
     * can use it in place of a value and the compiler knows the path ends.
     *
     * @throws std::runtime_error always.
     */
    [[noreturn]] void fail(const char* msg) const
    {
        throw std::runtime_error(std::string("json parse error at byte ") + std::to_string(pos) + ": " + msg);
    }

    /**
     * @brief The next character without consuming it.
     *
     * End of input reads as '\\0', which matches no production, so dispatch on
     * the next character needs no separate bounds test.
     */
    char peek() const
    {
        return pos < s.size() ? s[pos] : '\0';
    }

    /**
     * @brief Consumes and returns the next character.
     *
     * Unlike @ref peek this treats end of input as an error, for the positions
     * where the grammar requires another character to exist.
     *
     * @throws std::runtime_error at end of input.
     */
    char get()
    {
        if (pos >= s.size())
            fail("unexpected end of input");
        return s[pos++];
    }

    /**
     * @brief Consumes the next character, which must be @p c.
     * @throws std::runtime_error if the next character differs, or input ended.
     */
    void expect(char c)
    {
        if (get() != c)
            fail("unexpected character");
    }

    /**
     * @brief Advances past any run of whitespace.
     *
     * Carriage return is deliberately absent, matching the set this parser was
     * written against; a CRLF document therefore fails. @see the file header.
     */
    void skipWhitespace()
    {
        while (pos < s.size() &&
             (s[pos] == ' ' ||
              s[pos] == '\t' ||
              s[pos] == '\n'))
            ++pos;
    }

    /**
     * @brief Consumes @p lit if it sits at the cursor, leaving it otherwise.
     *
     * Used for the three bare keywords. Comparing through `substr` is safe at
     * the end of the text, where the shorter view simply fails to match.
     *
     * @return True if the literal matched and the cursor advanced past it.
     */
    bool consumeLiteral(std::string_view lit)
    {
        if (s.substr(pos, lit.size()) == lit)
        {
            pos += lit.size();
            return true;
        }
        return false;
    }

    /**
     * @brief Parses one JSON value of any type, recursing into containers.
     *
     * The entry point of the grammar, and the function every container calls
     * back into for its children. The value's type is decided by its first
     * character, which JSON makes unambiguous.
     *
     * Numbers are the default branch rather than a case: they start with a
     * digit or '-', so anything that is not a container, a string, or one of the
     * three keywords is handed to @ref parseNumberLiteral, which reports the
     * error if it is not a number either.
     *
     * @return The parsed value. Object members are named by the caller.
     * @throws std::runtime_error on malformed input.
     */
    JsonNode parseValue()
    {
        skipWhitespace();
        JsonNode n;
        switch (peek())
        {
            case '{':
            {
                parseObjectInto(n);
                break;
            }
            case '[':
            {
                parseArrayInto(n);
                break;
            }
            case '"':
            {
                n.type = JsonNode::STRING;
                n.strValue = parseStringLiteral();
                break;
            }
            case 't' :
            {
                if (!consumeLiteral("true")) fail("bad literal");
                n.type = JsonNode::BOOL;
                n.boolValue = true;
                break;
            }
            case 'f':
            {
                if (!consumeLiteral("false")) fail("bad literal");
                n.type = JsonNode::BOOL;
                n.boolValue = false;
                break;
            }
            case 'n':
            {
                if (!consumeLiteral("null")) fail("bad literal");
                n.type = JsonNode::NILL;
                break;
            }
            default:
            {
                n.type = JsonNode::NUMBER;
                n.numValue = parseNumberLiteral();
                break;
            }
        }
        return n;
    }

    /**
     * @brief Parses an object body into @p n, from '{' through its '}'.
     *
     * Fills a node in place rather than returning one so the children are built
     * directly in their final home, with no container copied on the way out.
     *
     * Members are appended in document order and keep it, which is what lets
     * @ref JsonNode::at walk an object positionally. Duplicate keys are both
     * stored; @ref JsonNode::find will return the first.
     *
     * @param n Node to fill; its type is set to OBJECT.
     * @throws std::runtime_error on a non-string key, a missing ':' or ',', or
     *         input ending before the closing '}'.
     */
    void parseObjectInto(JsonNode& n)
    {
        n.type = JsonNode::OBJECT;
        expect('{');
        skipWhitespace();
        if (peek() == '}')
        {
            ++pos;
            return;
        }
        while (true)
        {
            skipWhitespace();
            if (peek() != '"') fail("expected string key");
            std::string key = parseStringLiteral();
            skipWhitespace();
            expect(':');
            JsonNode child = parseValue();
            child.name = std::move(key);
            n.children.push_back(std::move(child));
            skipWhitespace();
            char c = get();
            if (c == ',') continue;
            if (c == '}') break;
            fail("expected ',' or '}'");
        }
    }

    /**
     * @brief Parses an array body into @p n, from '[' through its ']'.
     *
     * The counterpart to @ref parseObjectInto, and fills in place for the same
     * reason. Elements are unnamed, so each child keeps the empty @ref
     * JsonNode::name @ref parseValue left it with.
     *
     * The empty array is handled before the loop, since the loop expects at
     * least one element and would otherwise read ']' as a value.
     *
     * @param n Node to fill; its type is set to ARRAY.
     * @throws std::runtime_error on a missing ',' or input ending before ']'.
     */
    void parseArrayInto(JsonNode& n)
    {
        n.type = JsonNode::ARRAY;
        expect('[');
        skipWhitespace();
        if (peek() == ']')
        {
            ++pos;
            return;
        }
        while (true)
        {
            JsonNode child = parseValue();
            n.children.push_back(std::move(child));
            skipWhitespace();
            char c = get();
            if (c == ',') continue;
            if (c == ']') break;
            fail("expected ',' or ']'");
        }
    }

    /**
     * @brief Appends one code point to @p out as UTF-8.
     *
     * JSON escapes name code points, but the parsed strings are plain bytes, so
     * every `\\u` escape has to be encoded on the way out. The four branches are
     * the four UTF-8 lengths, chosen by how many bits @p cp needs.
     *
     * @p cp is not validated. A lone surrogate, which @ref parseStringLiteral
     * can hand over when a pair is not recombined, encodes as three bytes in the
     * CESU-8 style rather than being rejected or replaced.
     *
     * @param out Destination, appended to.
     * @param cp  Code point to encode.
     */
    static void appendUtf8(std::string& out, uint32_t cp)
    {
        if (cp <= 0x7F)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    /**
     * @brief Reads the four hex digits of a `\\u` escape.
     *
     * Called with the cursor just past the 'u'. Decodes by hand rather than
     * through `from_chars` so that a non-hex digit is an error here instead of a
     * short read the caller would have to notice.
     *
     * @return The 16-bit value; may be half of a surrogate pair.
     * @throws std::runtime_error if fewer than four characters remain, or any is
     *         not a hex digit.
     */
    uint32_t parseHex4()
    {
        if (pos + 4 > s.size()) fail("truncated \\u escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            char c = s[pos++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }

    /**
     * @brief Parses a quoted string, decoding escapes into the returned bytes.
     *
     * Serves both string values and object keys, since JSON gives them the same
     * syntax. The result is a fresh `std::string` rather than a view, because an
     * escaped string does not appear literally in the source text.
     *
     * Escapes are decoded here, so nothing downstream has to know they existed.
     * `\\u` escapes become UTF-8 through @ref appendUtf8, with a surrogate pair
     * recombined into the one code point it stands for.
     *
     * Unescaped control characters are accepted rather than rejected, which the
     * spec forbids but which no input here relies on.
     *
     * @return The decoded contents, without the surrounding quotes.
     * @throws std::runtime_error on an unknown escape, a malformed `\\u`, or
     *         input ending before the closing quote.
     */
    std::string parseStringLiteral()
    {
        expect('"');
        std::string out;
        while (true)
        {
            char c = get();
            if (c == '"') break;
            if (c == '\\')
            {
                char e = get();
                switch (e)
                {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u':
                    {
                        uint32_t cp = parseHex4();
                        if (cp >= 0xD800 && cp <= 0xD8FF)
                        {
                            if (pos + 1 < s.size() && s[pos] == '\\' && s[pos + 1] == 'u')
                            {
                                pos += 2;
                                uint32_t low = parseHex4();
                                if (low >= 0xDC00 && low <= 0xDFFF)
                                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xDC00);
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: fail("bad escape sequence");
                }
            }
            else
            {
                out.push_back(c);
            }
        }
        return out;
    }

    /**
     * @brief Parses a numeric literal.
     *
     * Two passes: the loops walk the cursor over the sign, integer, fraction,
     * and exponent to find where the number ends, then `from_chars` converts the
     * span that was spanned. Scanning first is what makes the conversion safe on
     * a borrowed, non-terminated view, and it keeps the number from running into
     * whatever punctuation follows.
     *
     * The scan is more permissive than the grammar — it accepts a leading zero,
     * a bare '-', or an empty exponent — but `from_chars` rejects what it cannot
     * convert, so malformed input still fails rather than parsing to a wrong
     * value.
     *
     * @return The value as a double, JSON's only numeric type.
     * @throws std::runtime_error when no number is present, or the span does not
     *         convert.
     */
    double parseNumberLiteral()
    {
        size_t start = pos;
        if (peek() == '-') ++pos;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        if (peek() == '.')
        {
            ++pos; while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (peek() == 'e' || peek() == 'E')
        {
            ++pos;
            if (peek() == '+' || peek() == '-') ++pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (pos == start) fail("invalid number");
        double d = 0.0;
        auto [ptr, ec] = std::from_chars(s.data() + start, s.data() + pos, d);
        (void)ptr;
        if (ec != std::errc()) fail("invalid number");
        return d;
    }
};

/**
 * @brief Parses JSON text into a node tree.
 *
 * The normal entry point. The returned tree owns everything it holds, so
 * @p text may go away immediately afterwards.
 *
 * @param text A complete JSON document.
 * @return The root node.
 * @throws std::runtime_error on malformed input, naming the byte offset.
 */
inline JsonNode parse(std::string_view text)
{
    return JsonParser(text).parseDocument();
}

/**
 * @brief Reads a file and parses it as JSON.
 *
 * The whole file is read into one buffer before parsing rather than streamed,
 * which is what lets the parser work on a contiguous view and report a single
 * byte offset for any error. It also means peak memory holds the text and the
 * tree at once — worth knowing for the 25 MB grammar, and part of why that one
 * is flattened into @ref cli::tree::CommandTree instead of being parsed at every
 * startup.
 *
 * Opened in binary mode so no byte is rewritten on the way in.
 *
 * @param path Filesystem path to the document.
 * @return The root node.
 * @throws std::runtime_error if the file cannot be opened, ends early, or does
 *         not parse.
 */
inline JsonNode load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("utils::json::load: cannot open " + path);
    auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string buf(size, '\0');
    if (size && !f.read(buf.data(), static_cast<std::streamsize>(size)))
        throw std::runtime_error("utils::json::load: short read on " + path);
    return parse(buf);
}
}

#endif // JSON_PARSER_HPP
