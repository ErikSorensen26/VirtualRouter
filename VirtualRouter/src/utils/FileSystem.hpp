/**
 * @file FileSystem.hpp
 * @brief Filesystem abstraction used for all router persistence.
 */

#ifndef FILE_SYSTEM_HPP
#define FILE_SYSTEM_HPP

#include <filesystem>
#include <fstream>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Mock.hpp>

namespace cli
{
/**
 * @brief Concrete filesystem implementation used by the router process.
 *
 * Backed by POSIX operations including `open`, `mmap`, and `std::ofstream`.
 *
 * ### Memory & Ownership Model
 * - `readFile` uses `mmap` for large-file efficiency.
 * - Caller owns returned string content.
 *
 * ### Concurrency Model
 * - Not thread-safe; assumes serialization by the caller.
 */
class FileSystem
{
public:
    MOCK ~FileSystem() = default;

    MOCK bool readFile(const std::string& path, std::string& content)
    {
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;

        struct stat st;
        fstat(fd, &st);

        void* data = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) return false;

        content.assign((char*)data, st.st_size);
        munmap(data, st.st_size);
        close(fd);
        return true;
    }

    MOCK bool writeFile(const std::string& path, const std::string& content)
    {
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file.is_open())
        {
            return false; // Cannot open the file for writing
        }

        file << content;
        file.close();
        return true;
    }

    MOCK bool fileExists(const std::string& path)
    {
        return std::filesystem::exists(path);
    }

    MOCK void removeFile(const std::string& path)
    {

    }
};
}

#endif // FILE_SYSTEM_HPP
