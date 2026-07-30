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
    explicit Storage(std::vector<std::byte> v);
    ~Storage();

    Storage(Storage&& o) noexcept;
    Storage& operator=(Storage&& o) noexcept;
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    static Storage mapFile(const std::string& path);

    const std::byte* data() const;

    size_t size() const { return mapped ? mappedSize : heap.size(); }
    bool isMapped() const { return mapped != nullptr; }

private:
    void unmap();

    std::vector<std::byte> heap;
    void* mapped = nullptr;
    size_t mappedSize = 0;
};
}

#endif // GRAMMAR_STORAGE_HPP
