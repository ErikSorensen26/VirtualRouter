/**
 * @file TlvOptions.hpp
 * @brief TLV (Type-Length-Value) option parsing and buffer management for 8-bit and 16-bit types.
 */

#ifndef TLV_OPTIONS_HPP
#define TLV_OPTIONS_HPP

#include <Likely.hpp>
#include <cstdint>
#include <cstring>
#include <span>

namespace packet
{

/**
 * @brief TLV option with 8-bit type and length fields.
 * @ingroup PACKET
 *
 * Represents a parsed TLV option from a buffer: 1-byte type, 1-byte length,
 * followed by variable-length value. Used for DHCP options, ICMPv6 options, etc.
 */
struct TLV8Option
{
    uint8_t type;              ///< Option type (1 byte).
    uint8_t length;            ///< Option length (1 byte).
    uint8_t* value;            ///< Pointer to option value bytes (may be nullptr if empty).
    size_t valueSize;          ///< Size of value in bytes.

    /**
     * @brief Gets option value as std::span<const uint8_t>.
     * @return Span over the value bytes.
     */
    std::span<const uint8_t> asSpan() const {
        return { value, valueSize };
    }
};

/**
 * @brief TLV option with 16-bit type and length fields (big-endian).
 * @ingroup PACKET
 *
 * Represents a parsed TLV option with larger field widths: 2-byte type, 2-byte length,
 * followed by variable-length value. Used for larger option sets (BGP, EIGRP, etc.).
 */
struct TLV16Option
{
    uint16_t type;             ///< Option type (2 bytes, network byte order).
    uint16_t length;           ///< Option length (2 bytes, network byte order).
    uint8_t* value;            ///< Pointer to option value bytes (may be nullptr if empty).
    size_t valueSize;          ///< Size of value in bytes.

    /**
     * @brief Gets option value as std::span<const uint8_t>.
     * @return Span over the value bytes.
     */
    std::span<const uint8_t> asSpan() const {
        return { value, valueSize };
    }
};

/**
 * @brief Builder for appending TLV8 options to a buffer.
 * @ingroup PACKET
 *
 * Manages a buffer and appends TLV options with 1-byte type and 1-byte length.
 * Tracks offset to avoid overwrites; callers should check hasRoom() before appending
 * and retrieve the final data with getSpan() or size().
 */
class TLV8BufferManager
{
private:
    uint8_t* buffer;           ///< Underlying buffer (not owned; must remain valid).
    size_t offset;             ///< Current write offset in buffer.
    size_t len;                ///< Maximum buffer length.
    
public:
    /**
     * @brief Constructs buffer manager for a TLV8 buffer.
     *
     * @param buf Buffer to write TLVs into (not owned by this manager).
     * @param len Maximum buffer length in bytes. Default 1500.
     */
    TLV8BufferManager(uint8_t* buf, size_t len = 1500) : buffer(buf), offset(0), len(len) {}

    /**
     * @brief Gets pointer to the next option value field, reserving space for header.
     *
     * Returns a pointer to where the value should be written for a TLV with the given
     * value size. Caller must manually copy value data and then call append() or
     * manually update offset. Returns nullptr if insufficient space.
     *
     * @param size Expected value size in bytes.
     * @return Pointer to value location, or nullptr if no room (offset + 2 + size > len).
     *
     * @note Caller is responsible for ensuring the value is written before next append().
     */
    uint8_t* getNextValBuf(uint8_t size = 0)
    {
        if (offset + 2 + size > len)
            return nullptr;
        else return buffer + offset + 2;
    }

    /**
     * @brief Increases maximum buffer length (typically called before resize).
     *
     * @param length Additional bytes to add to max length.
     */
    void addLen(size_t length)
    {
        len += length;
    }

    /**
     * @brief Checks if buffer has room for an option with given value size.
     *
     * @param size Expected value size in bytes.
     * @return True if offset + 2 (header) + size <= len.
     */
    bool hasRoom(size_t size)
    {
        return (offset + 2 + size <= len);
    }

    /**
     * @brief Appends a TLV8 option to the buffer.
     *
     * Writes type (1 byte), length (1 byte), and value (valueSize bytes).
     * Advances offset on success.
     *
     * @param type Option type (1 byte).
     * @param length Option length value to encode (1 byte). Typically should match valueSize.
     * @param value Pointer to value bytes (may be nullptr if valueSize is 0).
     * @param valueSize Number of value bytes to copy.
     * @return True on success, false if insufficient buffer space.
     */
    bool append(uint8_t type, uint8_t length, const uint8_t* value, size_t valueSize)
    {
        if (offset + 2 + valueSize > len) return false;

        // Write the type and length fields
        buffer[offset++] = type;
        buffer[offset++] = length;

        // Write the value field
        if (value)
        {
            std::memcpy(&buffer[offset], value, valueSize);
        }
        offset += valueSize;
        return true;
    }

    /**
     * @brief Appends a termination byte (e.g., end-of-options marker).
     *
     * @param term Termination byte to append.
     * @return True on success, false if insufficient buffer space.
     */
    bool addTermination(uint8_t term)
    {
        if (offset + 1 > len) return false;

        buffer[offset++] = term;
        return true;
    }

    /**
     * @brief Gets the current buffer contents as a const span.
     * @return Span over [buffer, offset).
     */
    std::span<const uint8_t> getSpan() const { return { buffer, offset }; }

    /**
     * @brief Gets the current number of bytes written.
     * @return Current offset.
     */
    size_t size() const { return offset; }

    /**
     * @brief Gets the maximum buffer size.
     * @return Maximum buffer length.
     */
    size_t maxSize() const { return len; }
};

/**
 * @brief Builder for appending TLV16 options to a buffer.
 * @ingroup PACKET
 *
 * Manages a buffer and appends TLV options with 2-byte (network-order) type and 2-byte length.
 * Typically used for protocol options with larger type/length fields (BGP, EIGRP, etc.). Callers
 * should check hasRoom() before appending and retrieve the final data with getSpan() or size().
 */
class TLV16BufferManager
{
private:
    uint8_t* buffer;           ///< Underlying buffer (not owned; must remain valid).
    size_t offset;             ///< Current write offset in buffer.
    size_t len;                ///< Maximum buffer length.
    
public:
    /**
     * @brief Constructs buffer manager for a TLV16 buffer.
     *
     * @param buf Buffer to write TLVs into (not owned by this manager).
     * @param len Maximum buffer length in bytes.
     */
    TLV16BufferManager(uint8_t* buf, size_t len) : buffer(buf), offset(0), len(len) {}

    /**
     * @brief Gets pointer to the next option value field, reserving space for 4-byte header.
     *
     * Returns a pointer to where the value should be written for a TLV with the given
     * value size. Caller must manually copy value data and then call append() or
     * manually update offset. Returns nullptr if insufficient space.
     *
     * @param size Expected value size in bytes.
     * @return Pointer to value location, or nullptr if no room (offset + 4 + size > len).
     *
     * @note Caller is responsible for ensuring the value is written before next append().
     */
    uint8_t* getNextValBuf(uint16_t size = 0)
    {
        if (offset + 4 + size > len)
            return nullptr;
        else return buffer + offset + 4;
    }

    /**
     * @brief Increases maximum buffer length (typically called before resize).
     *
     * @param length Additional bytes to add to max length.
     */
    void addLen(size_t length)
    {
        len += length;
    }

    /**
     * @brief Checks if buffer has room for an option with given value size.
     *
     * @param size Expected value size in bytes.
     * @return True if offset + 4 (header) + size <= len.
     */
    bool hasRoom(size_t size)
    {
        return (offset + 4 + size <= len);
    }

    /**
     * @brief Appends a TLV16 option to the buffer.
     *
     * Writes type (2 bytes, network order), length (2 bytes, network order),
     * and value (valueSize bytes). Advances offset on success.
     *
     * @param type Option type (2 bytes, converted to network byte order).
     * @param length Option length value to encode (2 bytes, converted to network byte order).
     *               Typically should match valueSize.
     * @param value Pointer to value bytes (may be nullptr if valueSize is 0 or value is
     *              already written via getNextValBuf()).
     * @param valueSize Number of value bytes to copy (or skip if value is nullptr).
     * @return True on success, false if insufficient buffer space.
     */
    bool append(uint16_t type, uint16_t length, const uint8_t* value, size_t valueSize)
    {
        if (unlikely(offset + 4 + valueSize > len)) return false;

        // Write the type and length fields in network byte order
        buffer[offset++] = type >> 8;
        buffer[offset++] = type & 0xFF;
        buffer[offset++] = length >> 8;
        buffer[offset++] = length & 0xFF;

        // Write the value field (or skip if value is managed externally)
        if (value) {
            // Null assumes you used getNextValBuf and managed the value yourself with correct size
            std::memcpy(&buffer[offset], value, valueSize);
        }
        offset += valueSize;
        return true;
    }

    /**
     * @brief Gets the current buffer contents as a const span.
     * @return Span over [buffer, offset).
     */
    std::span<const uint8_t> getSpan() const { return { buffer, offset }; }

    /**
     * @brief Gets the current number of bytes written.
     * @return Current offset.
     */
    size_t size() const { return offset; }

    /**
     * @brief Gets the maximum buffer size.
     * @return Maximum buffer length.
     */
    size_t maxSize() const { return len; }
};

} // namespace packet

#endif // TLV_OPTIONS_HPP

