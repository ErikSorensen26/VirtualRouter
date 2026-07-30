/**
 * @file FileHeader.hpp
 * @brief On-disk header for the flattened command tree.
 *
 * Describes the four sections that follow it in the file:
 *
 *     [FileHeader][ModeEntryNode[]][CommandNode[]][char blob]
 *
 * The counts let a reader bind spans over each section without walking it.
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
struct FileHeader
{
    static constexpr uint32_t CT_MAGIC = 0x5844494Au;
    static constexpr uint16_t CT_VERSION = 1;

    uint32_t magic      = CT_MAGIC;
    uint16_t version    = CT_VERSION;
    uint16_t headerSize = sizeof(FileHeader);
    uint32_t modeCount  = 0; // Entries, one per command list.
    uint32_t nodeCount  = 0;
    uint32_t blobSize   = 0;
    uint32_t reserved0  = 0;
    uint32_t reserved1  = 0;
};
}

#endif // GRAMMAR_FILE_HEADER_HPP
