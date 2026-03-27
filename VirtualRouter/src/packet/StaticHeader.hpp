/**
 * @file StaticHeader.hpp
 * @brief Fixed-size packet header wrapper with buffer management.
 */

#ifndef STATIC_HEADER_HPP
#define STATIC_HEADER_HPP

#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace packet
{

/**
 * @brief Owning buffer for a complete packet header (fixed + variable-length trailer).
 * @ingroup PACKET
 *
 * Wraps a malloc'd buffer containing a packet header. Supports move/copy semantics for
 * header passing between layers. Can extract a typed header object (using @ref get<T>())
 * if the header type provides a static setBuffer() interface.
 *
 * ## Concurrency Model
 * Not thread-safe; intended for single-threaded packet processing chains.
 */
struct StaticHeader
{
    uint8_t* buffer = nullptr;     ///< Malloc'd header buffer (may be nullptr if empty).
    size_t totalLen = 0;           ///< Total buffer length (fixed header + trailer).

    /**
     * @brief Constructs an empty StaticHeader.
     */
    StaticHeader() = default;

    /**
     * @brief Constructs StaticHeader by copying from a source buffer.
     *
     * Allocates and copies the entire source buffer. Safe to use even if @p src is nullptr
     * (results in empty header).
     *
     * @param src Source buffer to copy from (may be nullptr).
     * @param len Number of bytes to copy.
     */
    StaticHeader(const uint8_t* src, size_t len)
        : totalLen(len)
    {
        if (totalLen == 0) return;
        buffer = static_cast<uint8_t*>(std::malloc(totalLen));
        if (buffer) std::memcpy(buffer, src, totalLen);
    }

    /**
     * @brief Copy constructor: copies the buffer contents.
     *
     * Allocates new buffer and copies source buffer. Does not share memory.
     *
     * @param other Source StaticHeader to copy.
     */
    StaticHeader(const StaticHeader& other)
        : totalLen(other.totalLen)
    {
        if (!other.buffer || totalLen == 0) return;
        buffer = static_cast<uint8_t*>(std::malloc(totalLen));
        if (buffer) std::memcpy(buffer, other.buffer, totalLen);
    }

    /**
     * @brief Copy assignment: copies the buffer contents.
     *
     * Deallocates existing buffer and allocates new buffer with source contents.
     * Safe to assign to self (no-op).
     *
     * @param other Source StaticHeader to copy.
     * @return Reference to this.
     */
    StaticHeader& operator=(const StaticHeader& other)
    {
        if (this == &other) return *this;
        uint8_t* newBuf = nullptr;
        if (other.buffer && other.totalLen > 0)
        {
            newBuf = static_cast<uint8_t*>(std::malloc(other.totalLen));
            if (newBuf) std::memcpy(newBuf, other.buffer, other.totalLen);
        }
        std::free(buffer);
        buffer = newBuf;
        totalLen = other.totalLen;
        return *this;
    }

    /**
     * @brief Move constructor: transfers ownership of the buffer.
     *
     * Source is left with nullptr buffer and 0 length. No allocation/deallocation.
     *
     * @param other Source StaticHeader to move from (will be emptied).
     */
    StaticHeader(StaticHeader&& other) noexcept
        : buffer(other.buffer), totalLen(other.totalLen)
    {
        other.buffer = nullptr;
        other.totalLen = 0;
    }

    /**
     * @brief Move assignment: transfers ownership of the buffer.
     *
     * Deallocates existing buffer and takes ownership from source.
     * Source is left with nullptr buffer and 0 length. Safe to assign to self (no-op).
     *
     * @param other Source StaticHeader to move from (will be emptied).
     * @return Reference to this.
     */
    StaticHeader& operator=(StaticHeader&& other) noexcept
    {
        if (this == &other) return *this;
        if (other.buffer) std::free(buffer);
        buffer = other.buffer;
        totalLen = other.totalLen;
        other.buffer = nullptr;
        other.totalLen = 0;
        return *this;
    }

    /**
     * @brief Destructs and deallocates the buffer.
     */
    ~StaticHeader()
    {
        std::free(buffer);
    }

    /**
     * @brief Extracts a typed header object from the buffer.
     *
     * Creates a header of type T and calls T::setBuffer() to bind it to this buffer.
     * If the header type has a @p getTrail() method, also sets the trailer with any
     * remaining bytes after the fixed header size.
     *
     * @tparam T Header type. Must provide:
     *            - static constexpr size_t fixedSize (header size)
     *            - void setBuffer(uint8_t*) (attach to buffer)
     *            - optionally: void setTrail(uint8_t*, size_t) (for variable-length options)
     *
     * @return Header object bound to this buffer.
     *
     * @warning Returned header holds a pointer to this buffer; this buffer must not be
     * deallocated or moved while the header is in use.
     */
    template <typename T>
    T get()
    {
        T hdr;
        if (!buffer || totalLen < T::fixedSize)
            return hdr;
        hdr.setBuffer(buffer);
        if constexpr (requires(T h) { h.getTrail(); })
        {
            size_t trailLen = (totalLen > T::fixedSize)
                ? totalLen - T::fixedSize
                : 0;
            hdr.setTrail(buffer + T::fixedSize, trailLen);
        }
        return hdr;
    }

    /**
     * @brief Copies buffer contents to a destination.
     *
     * Copies up to @p maxSize bytes from the buffer to @p out. Returns 0 if buffer is
     * empty or too large for the output buffer.
     *
     * @param out Output buffer (must be at least @p maxSize bytes).
     * @param maxSize Maximum bytes to copy.
     * @return Number of bytes copied, or 0 if copy failed or buffer too large.
     */
    size_t copy(uint8_t* out, size_t maxSize) const
    {
        if (!buffer || totalLen == 0 || !out)
            return 0;

        size_t n = (totalLen <= maxSize) ? totalLen : 0;
        if (n == 0)
            return 0;

        std::memcpy(out, buffer, totalLen);
        return totalLen;
    }
};

} // namespace packet

#endif

