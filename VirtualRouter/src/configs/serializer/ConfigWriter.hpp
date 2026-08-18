/**
 * @file ConfigWriter.hpp
 * @brief Line-oriented output sink for the config serializer, with indent tracking.
 * @ingroup CONFIG_SERIALIZER
 *
 * Cisco-style running-config nests by indentation rather than by repeating an
 * `exit` for every mode entered -- `router ospf 1` / `\narea 1 range ...` reads
 * top to bottom with no closing line. @c ConfigWriter is exactly that: a depth
 * counter and a string sink, kept apart from the walker in ConfigSerializer.hpp
 * so the output format (indent width, line terminator) is a property of the
 * writer alone.
 */

#if 0
#ifndef CONFIG_SERIALIZER_CONFIG_WRITER_HPP
#define CONFIG_SERIALIZER_CONFIG_WRITER_HPP

#include <string>
#include <vector>

namespace config::serializer
{
class ConfigWriter
{
public:
    explicit ConfigWriter(unsigned indentWidth = 1) : width(indentWidth) {}

    /// @brief Writes one line at the current depth, e.g. "router ospf 1".
    void line(std::string_view text)
    {
        out.append(depth * width, ' ');
        out.append(text);
        out.push_back('\n');
    }

    /// @brief Joins @p words with single spaces and writes them as one line.
    void line(const std::vector<std::string>& words)
    {
        std::string joined;
        for (size_t i = 0; i < words.size(); ++i)
        {
            if (i) joined.push_back(' ');
            joined += words[i];
        }
        line(joined);
    }

    /// @brief Blank separator line, used between sibling scopes.
    void blank()
    {
        out.push_back('\n');
    }

    /// @brief Increases indent for the lines a nested scope writes.
    void push()
    {
        ++depth;
    }

    /// @brief Restores the indent level a scope was entered at.
    void pop()
    {
        if (depth) --depth;
    }

    /// @brief RAII helper so a scope's indent is always restored, exceptions included.
    class Scope
    {
    public:
        explicit Scope(ConfigWriter& w) : writer(w) { writer.push(); }
        ~Scope() { writer.pop(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        ConfigWriter& writer;
    };

    Scope scope() { return Scope(*this); }

    const std::string& str() const { return out; }
    std::string take() { return std::move(out); }

private:
    std::string out;
    unsigned depth = 0;
    unsigned width;
};
}

#endif // CONFIG_SERIALIZER_CONFIG_WRITER_HPP
#endif
