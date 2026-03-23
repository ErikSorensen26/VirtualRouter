
#ifndef HEADER_HELPER_HPP
#define HEADER_HELPER_HPP

#include <cstring>
#include <arpa/inet.h>
#include <AddressFamily.hpp>
#include <Likely.hpp>
#include <ByteUtils.hpp>

#define DEFINE_PACKET_HEADER(RAWTYPE)                                                               \
    using RawType = RAWTYPE;                                                                        \
    inline static constexpr size_t fixedSize = sizeof(RawType);                                     \
    RawType* raw = nullptr;                                                                         \
    uint8_t* buffer = nullptr;                                                                      \
    std::span<uint8_t> trailing;                                                                    \
                                                                                                    \
    void setBuffer(uint8_t* buf)                                                                    \
    {                                                                                               \
        raw = reinterpret_cast<RAWTYPE*>(buf);                                                      \
        buffer = buf;                                                                               \
        trailing = std::span<uint8_t>(reinterpret_cast<uint8_t*>(raw) + fixedSize, 0);              \
    }                                                                                               \
                                                                                                    \
    bool parse(uint8_t* data, size_t len, size_t headerSize, size_t startIndex = 0)                 \
    {                                                                                               \
        if (!data || len < startIndex + headerSize || len < startIndex + fixedSize) return false;   \
        raw = reinterpret_cast<RAWTYPE*>(const_cast<uint8_t*>(data + startIndex));                  \
        size_t remaining = headerSize - fixedSize;                                                  \
        trailing = std::span<uint8_t>(data + startIndex + fixedSize, remaining);                    \
        return true;                                                                                \
    }                                                                                               \
                                                                                                    \
    uint16_t encapsulate(uint8_t* out, size_t startIndex, size_t maxSize) const                     \
    {                                                                                               \
        if (!raw || !out) return 0;                                                                 \
        size_t totalSize = fixedSize + trailing.size();                                             \
        if (startIndex + totalSize > maxSize) return 0;                                             \
        const uint8_t* base = reinterpret_cast<const uint8_t*>(raw);                                \
        std::memcpy(out + startIndex, base, fixedSize);                                             \
        if (!trailing.empty()) {                                                                    \
            std::memcpy(out + startIndex + fixedSize, trailing.data(), trailing.size());            \
        }                                                                                           \
        return totalSize;                                                                           \
    }                                                                                               \
                                                                                                    \
    std::span<uint8_t> getTrail() const { return trailing; }                                        \
    uint8_t* getTrailData() { return trailing.data(); }                                             \
    const uint8_t* getTrailData() const { return trailing.data(); }                                 \
    void setTrail(uint8_t* data, size_t len)                                                        \
    {                                                                                               \
        std::memcpy(buffer + fixedSize, data, len);                                                 \
        trailing = std::span<uint8_t>(buffer + fixedSize, len);                                     \
    }                                                                                               \
    void setTrailSize(size_t len)                                                                   \
    {                                                                                               \
        trailing = std::span(buffer + fixedSize, len);                                              \
    }                                                                                               \
    void addTrailSize(size_t len)                                                                   \
    {                                                                                               \
        trailing = std::span(buffer + fixedSize, trailing.size() + len);                            \
    }                                                                                               \
                                                                                                    \
    size_t size() const { return trailing.size() + fixedSize; }


#define DEFINE_FIXED_HEADER(RAWTYPE)                                                                \
    static constexpr size_t fixedSize = sizeof(RAWTYPE);                                            \
    RAWTYPE* raw = nullptr;                                                                         \
                                                                                                    \
    void setBuffer(uint8_t* buffer)                                                                 \
    {                                                                                               \
        raw = reinterpret_cast<RAWTYPE*>(buffer);                                                   \
    }                                                                                               \
                                                                                                    \
    bool parse(const uint8_t* data, size_t len, size_t startIndex = 0)                              \
    {                                                                                               \
        if (!data || len < startIndex + fixedSize) return false;                                    \
        raw = reinterpret_cast<RAWTYPE*>(const_cast<uint8_t*>(data + startIndex));                  \
        return true;                                                                                \
    }                                                                                               \
                                                                                                    \
    uint16_t encapsulate(uint8_t* out, size_t startIndex, size_t maxSize) const                     \
    {                                                                                               \
        if (!raw || !out) return 0;                                                                 \
        if (startIndex + fixedSize > maxSize) return 0;                                             \
        const uint8_t* base = reinterpret_cast<const uint8_t*>(raw);                                \
        std::memcpy(out + startIndex, base, fixedSize);                                             \
        return fixedSize;                                                                           \
    }

#endif

