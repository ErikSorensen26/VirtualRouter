/**
 * @file CommandTree.h
 * @brief The CLI grammar, flattened once and mapped read-only at runtime.
 * @ingroup CLI_RUNTIME
 *
 * The grammar directory is parsed into a flat buffer of fixed-size records and
 * a string blob, cached to disk, and thereafter @c mmap ed. Nothing is allocated
 * per traversal: @c ModeEntry and @c Command are cursors into the mapping, and
 * every string is a @c string_view pointing at the blob.
 *
 * Children are laid out contiguously by the flattener, so a node locates them
 * with an offset and a count. Every accessor here bounds-checks against the
 * header's counts, because a bad index would otherwise read a valid but
 * unrelated record instead of failing.
 *
 * Lookups are keyed by @c CliMode, whose path is either a mode name or a mode
 * plus one submode. Submodes never nest deeper, which keeps the mode table flat.
 *
 * The cache is rebuilt rather than trusted whenever the build it was written by
 * no longer matches: a format change bumps the header's version, a registry
 * change shifts the ids a configId encodes, and an edit to the grammar files
 * changes their hash. Each is checked on open, since a stale tree read as valid
 * is a silent wrong answer rather than a failure.
 *
 * Cursors borrow from the tree and must not outlive it.
 */

#ifndef COMMAND_TREE_HPP
#define COMMAND_TREE_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <span>
#include <unordered_map>
#include "Storage.h"
#include "nodes/Command.h"
#include "nodes/ModeEntry.h"
#include "TreePatch.h"

#include "cli/modes/Mode.hpp"

namespace utils::json { struct JsonNode; }

namespace cli::tree
{
/// @brief Interface type name to configured port count, from the hardware config.
using PortCounts = std::unordered_map<std::string, size_t>;

namespace parser
{
std::vector<std::byte> flattenDir(const std::string& dir);

/**
 * @brief Hashes a grammar directory's contents, for cache staleness checks.
 *
 * Covers each .json file's path relative to @p dir and its bytes, in sorted
 * order, so an edit, rename, addition or removal all change the result while a
 * move of the directory itself does not.
 *
 * Returns 0 when the directory cannot be read. That reads as "unknown" rather
 * than as a particular hash, and callers treat it as a non-match, since the
 * alternative is trusting a cache whose sources could not be checked.
 */
uint32_t hashDir(const std::string& dir);
}

struct FileHeader;

class CommandTree
{
public:
    CommandTree() = default;

    /**
     * @brief Adopts a tree the flattener just produced, with no cache involved.
     *
     * The in-memory path, used by tests and by anything building a grammar it
     * does not want to persist. There is no source directory to compare against,
     * so the staleness checks that guard the cached path do not apply.
     */
    explicit CommandTree(std::vector<std::byte> builtBuffer);

    /// @brief Adopts already-owned bytes, mapped or heap; see @ref Storage.
    explicit CommandTree(Storage storage);

    /**
     * @brief Opens the flattened tree, building it from source when unusable.
     *
     * Maps binaryPath when it already holds a tree matching this build and this
     * grammar. Otherwise parses sourcePath, writes the flattened result to
     * binaryPath, and maps that, so the parse cost is paid once per grammar
     * change rather than per startup.
     *
     * A binary is rebuilt rather than rejected when it is truncated, from an
     * older format, built against a different registry list, or flattened from
     * grammar files that have since changed. The last is what makes an edit to
     * a .json file take effect without deleting the cache by hand.
     *
     * @param sourcePath Directory of per-mode grammar files.
     * @param binaryPath Flattened cache to read or regenerate.
     */
    CommandTree(const std::string& sourcePath, const std::string& binaryPath);

    /**
     * @brief Parses the grammar directory and writes the flattened tree to
     *        binaryPath.
     */
    static void build(const std::string& sourcePath, const std::string& binaryPath);

    /// @brief The grammar hash recorded in the mapped file, or 0 if unbound.
    uint32_t grammarHash() const;

    /**
     * @brief Reads per-interface-type port counts from a hardware config file.
     *
     * Returns an empty map when the file is missing or malformed, which leaves
     * port placeholders unresolved rather than failing session startup.
     */
    static PortCounts readPortCounts(const std::string& hwConfigPath);

    /**
     * @brief Numbers the grammar's bare "<N>" port placeholders from @p counts.
     *
     * The grammar cannot know how many ports a chassis has, so it writes a lone
     * "<N>" wherever an interface number belongs. This resolves each one against
     * the port count for the interface type it hangs off, turning "<0>" under
     * GigabitEthernet on a 10-port box into "<0-9>". Afterwards the tree reads as
     * though the range had been there all along.
     *
     * An interface type with no configured ports keeps its placeholder: matching
     * nothing is correct for a type the hardware does not have, and inventing a
     * range would accept numbers for ports that cannot exist.
     *
     * Safe to call more than once; each call recomputes from the mapping.
     */
    void applyPortCounts(const PortCounts& counts);

    /// @brief The command list for a mode; an invalid cursor when unmatched.
    ModeEntry getMode(CliMode mode) const;

    /// @brief The mode entry at a flat table index.
    ModeEntry modeEntry(size_t index) const;

    /// @brief Number of mode entries, one per command list.
    size_t modeCount() const;

    /// @brief Index of a plain mode, or NPOS when it routes to submodes.
    size_t findMode(const std::string_view modeName) const;

    /// @brief Index of a submode entry, or NPOS when absent.
    size_t findMode(const std::string_view modeName, std::string_view subName) const;

    /// @brief Returned by @ref findMode for a mode that is not in the table.
    static constexpr size_t NPOS = ~size_t{0};

private:
    friend class Command;
    friend class ModeEntry;

    /**
     * @brief Validates the header and binds a span over each section.
     *
     * @throws std::runtime_error when the buffer is too small, carries the wrong
     *         magic or version, was built against a different registry list, or
     *         is shorter than its own header describes. Each would otherwise
     *         read a valid-looking record out of unrelated bytes.
     */
    void bindBase();

    /**
     * @brief Text of an interned string, or empty for STR_NONE.
     *
     * Ids at or above @ref TreePatch::ID_BIAS resolve against the patch rather
     * than the mapping, which is how a renamed node reads back its new text
     * without the shared table entry -- one `<N>` serves every interface type --
     * being disturbed.
     */
    std::string_view strText(uint16_t id) const;

    /// @brief The node at a flat index, patched if it has an override.
    const CommandNode& nodeAt(uint32_t idx) const;

    /// @brief The mode entry at a flat index.
    const ModeEntryNode& modeAt(uint32_t idx) const;

    /// @brief Flat index of a parent's nth child; children are contiguous.
    uint32_t childIndex(const CommandNode& parent, uint32_t ordinal) const;

    Storage storage;
    TreePatch patch; ///< Runtime name overrides layered over the mapping.
    const FileHeader* header = nullptr;
    std::span<const ModeEntryNode> baseModes;
    std::span<const CommandNode> baseNodes;
    std::span<const StrRef> baseStrs;
    std::span<const char> baseBlob;
};
}

#endif // COMMAND_TREE_HPP
