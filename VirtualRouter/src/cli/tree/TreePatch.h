/**
 * @file TreePatch.h
 * @brief Runtime edits layered over the read-only command tree.
 * @ingroup CLI_RUNTIME
 *
 * Some of the grammar is not knowable when the grammar directory is compiled into
 * a @c Commands.bin binary. Port numbering is the first case: the grammar writes
 * a bare @c "<N>" for an interface number because how many ports exist is a property
 * of the hardware, not of the grammar, and the same binary has to serve every chassis.
 *
 * Baking the answer into the binary would tie the cache to one machine's
 * hardware config, and resolving it at every match site would spread the same
 * lookup across the traversal, help, and completion paths. Instead the tree is
 * patched once at load: the mapping stays untouched and a @c TreePatch holds
 * the replacement text, so from then on every reader sees @c "<0-9>" and no
 * consumer needs to know a substitution happened.
 *
 * The mapping cannot be written to. It is mapped @c PROT_READ, and the strings
 * it holds are interned -- one @c "<N>" entry is shared by every interface type
 * carrying a port placeholder -- so editing an entry in place would rename every
 * user of it at once. Patched text is appended to a separate buffer instead and
 * given an id of its own, and the node's name id is redirected to it.
 *
 * Patches are keyed by node index and applied by @ref CommandTree::nodeAt, so
 * anything reached through a cursor observes them. Adding a new kind of runtime
 * edit means writing another pass over the tree that calls @ref patchName —
 * nothing downstream changes.
 */

#ifndef GRAMMAR_TREE_PATCH_H
#define GRAMMAR_TREE_PATCH_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "nodes/Command.h"
#include "nodes/FileHeader.hpp"

namespace cli::tree
{
/**
 * @brief Reads the first index out of a bare `<N>` port placeholder.
 *
 * The grammar writes interface numbering as a lone `<N>` because the port count
 * belongs to the hardware, not the grammar. A pattern that already carries a
 * range (`<1-99>`) is not a placeholder and yields nothing.
 *
 * @param pattern Candidate pattern, e.g. `"<0>"`.
 * @return The starting index, or nullopt when `pattern` is not a placeholder.
 */
std::optional<uint64_t> portPlaceholderBase(std::string_view pattern);

/**
 * @brief Expands a `<N>` placeholder into the concrete range for `count` ports.
 *
 * `<0>` with 10 ports becomes `<0-9>`. Returns an empty string when `pattern` is
 * not a placeholder or the count is zero, which leaves the caller on its normal
 * path rather than writing a range no interface can satisfy.
 */
std::string expandPortPlaceholder(std::string_view pattern, size_t count);

/**
 * @brief Replacement names for nodes whose text is decided at runtime.
 *
 * Holds an append-only text buffer plus one patched @ref CommandNode per edited
 * index. The original node is copied and only its name offset and length are
 * repointed, so flags, description, and children are carried over untouched.
 */
class TreePatch
{
public:
    /**
     * @brief True when no node has been patched, so readers can skip the lookup.
     */
    bool empty() const { return overrides.empty(); }

    /**
     * @brief Redirects one node's name to @p name.
     *
     * The replacement gets a new id rather than editing the one the node had:
     * that id is interned and shared, so writing through it would rename every
     * other node using the same text.
     *
     * @param index Node index in the tree's flat node array.
     * @param base  The node as it appears in the mapping; copied, not modified.
     * @param name  Replacement name.
     */
    void patchName(uint32_t index, const CommandNode& base, std::string_view name);

    /**
     * @brief The patched node for @p index, or nullptr when it is unpatched.
     *
     * The returned reference stays valid for the life of the patch. Text is
     * appended to a @c std::string whose growth would invalidate views into it,
     * so patched strings are addressed by offset and resolved late.
     */
    const CommandNode* find(uint32_t index) const;

    /**
     * @brief Text of a patched string id; empty when @p id is not one.
     */
    std::string_view text(uint16_t id) const;

    /**
     * @brief Marks a string id as belonging to the patch rather than the table.
     *
     * Patched names live in a different buffer than the mapped blob, so a
     * reader has to know which one an id refers to. Ids at or above this bias
     * are patch entries; everything below indexes the interned table.
     *
     * Sits at half the id space, which caps patched strings at 32767 -- port
     * placeholders number in the dozens, and the flattener already rejects a
     * grammar with more than ID_BIAS interned strings.
     */
    static constexpr uint16_t ID_BIAS = 0x8000u;

private:
    std::string blob;                                    ///< Patched text, back to back.
    std::vector<StrRef> strs;                            ///< One entry per patched string; index + ID_BIAS is its id.
    std::unordered_map<uint32_t, CommandNode> overrides; ///< Patched nodes, keyed by their index in the base array.
};
}

#endif // GRAMMAR_TREE_PATCH_H
