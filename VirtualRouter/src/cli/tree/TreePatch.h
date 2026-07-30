/**
 * @file TreePatch.h
 * @brief Runtime edits layered over the read-only command tree.
 *
 * Some of the grammar is not knowable when @c Commands.json is flattened. Port
 * numbering is the first case: the grammar writes a bare @c "<N>" for an
 * interface number because how many ports exist is a property of the hardware,
 * not of the grammar, and the same binary has to serve every chassis.
 *
 * Baking the answer into the binary would tie the cache to one machine's
 * hardware config, and resolving it at every match site would spread the same
 * lookup across the traversal, help, and completion paths. Instead the tree is
 * patched once at load: the mapping stays untouched and a @c TreePatch holds
 * the replacement text, so from then on every reader sees @c "<0-9>" and no
 * consumer needs to know a substitution happened.
 *
 * The mapping cannot be written to. It is mapped @c PROT_READ, and even with
 * write access the string blob is packed contiguously, so a replacement longer
 * than the original would overrun the following name. Patched text is appended
 * to a separate buffer instead, and the node's offset and length are redirected
 * into it through an override table.
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

#include "Command.h"

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
    /// @brief True when no node has been patched, so readers can skip the lookup.
    bool empty() const { return overrides.empty(); }

    /**
     * @brief Redirects one node's name to @p name.
     *
     * @param index Node index in the tree's flat node array.
     * @param base  The node as it appears in the mapping; copied, not modified.
     * @param name  Replacement name. Truncated to the 255-byte limit of
     *              @c CommandNode::nameSiz, which no CLI token approaches.
     * @param desc  The node's existing description, copied in behind the name
     *              so it stays where @c desc() expects to find it.
     */
    void patchName(uint32_t index, const CommandNode& base,
                   std::string_view name, std::string_view desc);

    /**
     * @brief The patched node for @p index, or nullptr when it is unpatched.
     *
     * The returned reference stays valid for the life of the patch. Text is
     * appended to a @c std::string whose growth would invalidate views into it,
     * so patched nodes address the buffer by offset and are resolved late.
     */
    const CommandNode* find(uint32_t index) const;

    /// @brief Backing text for patched names; indexed by the patched infoOff.
    std::string_view text() const { return blob; }

    /**
     * @brief Marks offsets in a patched node as addressing @ref text.
     *
     * Patched names live in a different buffer than the mapped blob, so a
     * reader has to know which one an offset refers to. Offsets at or above
     * this bias belong to the patch; everything below is the original blob.
     */
    static constexpr uint32_t OFFSET_BIAS = 0x8000'0000u;

private:
    std::string blob;                                      ///< Patched names, each followed by its description.
    std::unordered_map<uint32_t, CommandNode> overrides;   ///< Patched nodes, keyed by their index in the base array.
};
}

#endif // GRAMMAR_TREE_PATCH_H
