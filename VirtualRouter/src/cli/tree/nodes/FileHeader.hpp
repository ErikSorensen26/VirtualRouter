/**
 * @file FileHeader.hpp
 * @brief On-disk header for the flattened command tree.
 *
 * Describes the sections that follow it in the file:
 *
 *     [FileHeader][ModeEntryNode[]][CommandNode[]][StrRef[]][char blob]
 *
 * The counts let a reader bind spans over each section without walking it.
 *
 * @c StrRef is the intern table: nodes hold an id into it rather than an offset
 * and length of their own, so a name repeated across the grammar is stored once.
 * @c magic and @c version are checked on open; a mismatch means the cache was
 * written by a different build and is regenerated rather than rejected, so
 * bumping @c CT_VERSION after any layout change is what keeps stale caches from
 * being read as valid.
 */

#ifndef GRAMMAR_FILE_HEADER_HPP
#define GRAMMAR_FILE_HEADER_HPP

#include <cstdint>

namespace cli::tree
{
/**
 * @brief One interned string: where it starts in the blob and how long it is.
 *
 * Nodes and mode entries address strings by id into this table. The length
 * lives here rather than on the node, so a name is bounded by the table's own
 * record instead of by a byte field the caller has to size correctly.
 */
struct StrRef
{
    uint32_t off = 0;
    uint32_t len = 0;
};

static_assert(sizeof(StrRef) == 8, "StrRef layout is the on-disk format");

struct FileHeader
{
    static constexpr uint32_t CT_MAGIC = 0x5844494Au;
    static constexpr uint16_t CT_VERSION = 8;

    uint32_t magic         = CT_MAGIC;
    uint16_t version       = CT_VERSION;
    uint16_t headerSize    = sizeof(FileHeader);
    uint32_t modeCount     = 0; // Entries, one per command list.
    uint32_t nodeCount     = 0;
    uint32_t strCount      = 0; // Interned strings.
    uint32_t blobSize      = 0;

    /**
     * config::REGISTRY_FIELD_HASH at the time this file was written.
     *
     * Both halves of a configId are positions: the registry id is a position in
     * REGISTRY_ID_LIST, the field index a position in that registry's enum. A
     * build that adds, removes, renames or reorders either shifts them, so a
     * mismatch here means the ids in this file no longer mean what this build
     * thinks they mean, and the tree is regenerated rather than misread.
     */
    uint32_t registryHash  = 0;

    /**
     * Hash of the grammar sources this file was flattened from.
     *
     * The other two checks catch a changed *build*; this catches changed
     * *input*. Editing a grammar file leaves the format and the registry alone,
     * so without this the cache still looks valid and the edit simply does not
     * appear -- which reads as the grammar being wrong rather than stale.
     *
     * Covers every file's name and contents, so an added, removed, renamed or
     * edited file all land here. Zero means "not recorded": a file written
     * before this field existed, which the version check already rejects.
     */
    uint32_t grammarHash   = 0;
};
}

#endif // GRAMMAR_FILE_HEADER_HPP
