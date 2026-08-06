/**
 * @file Storage.h
 * @brief Owns the bytes of a flattened command tree.
 *
 * Holds its buffer one of two ways: a read-only @c mmap of a cache file, or a
 * heap vector for a tree built in memory. Callers see only @c data() and
 * @c size(), so a CommandTree works the same either way.
 *
 * Move-only, because the mapping is released in the destructor and two owners
 * would unmap the same region twice.
 */

#ifndef GRAMMAR_STORAGE_HPP
#define GRAMMAR_STORAGE_HPP

#include <vector>
#include <string>

namespace cli::tree
{
class Storage
{
public:
    Storage() = default;

    /// @brief Takes ownership of an in-memory tree, as the flattener produces.
    explicit Storage(std::vector<std::byte> v);

    ~Storage();

    Storage(Storage&& o) noexcept;
    Storage& operator=(Storage&& o) noexcept;
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    /**
     * @brief Maps a cache file read-only.
     *
     * @throws std::runtime_error when the file cannot be opened, stat'd, or
     *         mapped, and when it is empty -- a zero-length mapping is not a
     *         valid tree and would fail later at a less obvious place.
     */
    static Storage mapFile(const std::string& path);

    /// @brief First byte of the tree, whichever way it is held; null when empty.
    const std::byte* data() const;

    /// @brief Size of the held buffer in bytes.
    size_t size() const { return mapped ? mappedSize : heap.size(); }

    /// @brief True when backed by a file mapping rather than the heap.
    bool isMapped() const { return mapped != nullptr; }

private:
    /// @brief Releases the mapping if there is one; safe to call repeatedly.
    void unmap();

    std::vector<std::byte> heap;
    void* mapped = nullptr;
    size_t mappedSize = 0;
};
}

#endif // GRAMMAR_STORAGE_HPP
