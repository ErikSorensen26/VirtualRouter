// Storage.cpp

#include "Storage.h"

#include <stdexcept>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace cli::tree
{
Storage::Storage(std::vector<std::byte> v) : heap(std::move(v)) {}
Storage::~Storage() { unmap(); }

Storage::Storage(Storage&& o) noexcept { *this = std::move(o); }
Storage& Storage::operator=(Storage&& o) noexcept
{
    if (this == &o) return *this;
    unmap();
    heap = std::move(o.heap);

    mapped = o.mapped;
    mappedSize = o.mappedSize;

    o.mapped = nullptr;
    o.mappedSize = 0;

    return *this;
}

Storage Storage::mapFile(const std::string& path)
{
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("cli::tree::Storage::mapFile: cannot open " + path);
    struct stat st{};
    if (::fstat(fd, &st) != 0)
    {
        ::close(fd);
        throw std::runtime_error("cli::tree::Storage::mapFile: fstat failed");
    }
    size_t sz = static_cast<size_t>(st.st_size);
    if (sz == 0)
    {
        ::close(fd);
        throw std::runtime_error("cli::tree::Storage::mapFile: empty file " + path);
    }
    void* p = ::mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED)
        throw std::runtime_error("cli::tree::Storage::mapFile: mmap failed");
    Storage s;
    s.mapped = p;
    s.mappedSize = sz;
    return s;
}

const std::byte* Storage::data() const
{
    return mapped ? static_cast<const std::byte*>(mapped) : heap.data();
}

void Storage::unmap()
{
    if (mapped)
    {
        ::munmap(mapped, mappedSize);
        mapped = nullptr;
        mappedSize = 0;
    }
}
}
