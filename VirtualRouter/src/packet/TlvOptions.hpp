// TLV16Options.hpp

#ifndef TLV_OPTIONS_HPP
#define TLV_OPTIONS_HPP

#include <likely.hpp>
#include <cstdint>
#include <cstring>
#include <HeaderHelpers.hpp>
#include <span>

struct TLV8Option
{
    uint8_t type;
    uint8_t length;
    uint8_t* value;
    size_t valueSize;

    std::span<const uint8_t> asSpan() const {
        return { value, valueSize };
    }
};

struct TLV16Option
{
    uint16_t type;
    uint16_t length;
    const uint8_t* value;
    size_t valueSize;

    std::span<const uint8_t> asSpan() const {
        return { value, valueSize };
    }
};

class TLV8BufferManager
{
private:
    uint8_t* buffer;
    size_t offset;
    size_t len;
    
public:
    TLV8BufferManager(uint8_t* buf, size_t len = 1500) : buffer(buf), offset(0), len(len) {}

    uint8_t* getNextValBuf(uint8_t size = 0)
    {
        if (offset + 2 + size > len)
            return nullptr;
        else return buffer + offset + 2;
    }

    void addLen(size_t length)
    {
        len += length;
    }

    bool hasRoom(size_t size)
    {
        return (offset + 2 + size <= len);
    }

    // Append a TLV8 option (1-byte type, 1 byte length followed by value)
    bool append(uint8_t type, uint8_t length, const uint8_t* value, size_t valueSize)
    {
        if (offset + 2 + valueSize > len) return false;

        // Write the type and length fields
        buffer[offset++] = type;
        buffer[offset++] = length;

        // Write the value field
        std::memcpy(&buffer[offset], value, valueSize);
        offset += valueSize;
        return true;
    }

    bool addTermination(uint8_t term)
    {
        if (offset + 1 > len) return false;

        buffer[offset++] = term;
        return true;
    }

    std::span<const uint8_t> getSpan() const { return { buffer, offset }; }
    size_t size() const { return offset; }
    size_t maxSize() const { return len; }
};

class TLV16BufferManager
{
private:
    uint8_t* buffer;
    size_t offset;
    size_t len;
    
public:
    TLV16BufferManager(uint8_t* buf, size_t len) : buffer(buf), offset(0), len(len) {}

    uint8_t* getNextValBuf(uint16_t size = 0)
    {
        if (offset + 4 + size > len)
            return nullptr;
        else return buffer + offset + 4;
    }

    void addLen(size_t length)
    {
        len += length;
    }

    bool hasRoom(size_t size)
    {
        return (offset + 4 + size <= len);
    }

    // Append a TLV8 option (1-byte type, 1 byte length followed by value)
    bool append(uint16_t type, uint16_t length, const uint8_t* value, size_t valueSize)
    {
        if (unlikely(offset + 4 + valueSize > len)) return false;

        // Write the type and length fields
        buffer[offset++] = type >> 8;
        buffer[offset++] = type & 0xFF;
        buffer[offset++] = length >> 8;
        buffer[offset++] = length & 0xFF;

        // Write the value field
        if (value) {
            // Null assumes you used getNextValBuf and managed the value yourself with correct size
            std::memcpy(&buffer[offset], value, valueSize);
        }
        offset += valueSize;
        return true;
    }

    std::span<const uint8_t> getSpan() const { return { buffer, offset }; }
    size_t size() const { return offset; }
    size_t maxSize() const { return len; }
};

#endif // TLV_OPTIONS_HPP
