/**
 * @file CommandTree.h
 * @brief The CLI grammar, flattened once and mapped read-only at runtime.
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
 * The file also carries a registry table and a slot table, which this class only
 * steps over. They exist so the flattener can reject two commands binding the
 * same config field; nothing reads them back at runtime.
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
}

struct FileHeader;

class CommandTree
{
public:
    CommandTree() = default;

    explicit CommandTree(std::vector<std::byte> builtBuffer);

    explicit CommandTree(Storage storage);

    /**
     * @brief Opens the flattened tree, building it from source when absent.
     *
     * Maps binaryPath when it already holds a usable tree. Otherwise parses
     * sourcePath, writes the flattened result to binaryPath, and maps that, so
     * the parse cost is paid once per grammar change rather than per startup.
     *
     * A binary that is stale, truncated, or from an older format is rebuilt
     * rather than rejected.
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

    ModeEntry getMode(CliMode mode) const;

    /// @brief The mode entry at a flat table index.
    ModeEntry modeEntry(size_t index) const;

    /// @brief Number of mode entries, one per command list.
    size_t modeCount() const;

    /// @brief Index of a plain mode, or NPOS when it routes to submodes.
    size_t findMode(const std::string_view modeName) const;

    /// @brief Index of a submode entry, or NPOS when absent.
    size_t findMode(const std::string_view modeName, std::string_view subName) const;

    static constexpr size_t NPOS = ~size_t{0};

private:
    friend class Command;
    friend class ModeEntry;

    void bindBase();

    std::string_view blobText(uint32_t off, uint32_t len) const;

    const CommandNode& nodeAt(uint32_t idx) const;

    const ModeEntryNode& modeAt(uint32_t idx) const;

    uint32_t childIndex(const CommandNode& parent, uint32_t ordinal) const;

    Storage storage;
    TreePatch patch; ///< Runtime name overrides layered over the mapping.
    const FileHeader* header = nullptr;
    std::span<const ModeEntryNode> baseModes;
    std::span<const CommandNode> baseNodes;
    std::span<const char> baseBlob;
};
}

#endif // COMMAND_TREE_HPP
