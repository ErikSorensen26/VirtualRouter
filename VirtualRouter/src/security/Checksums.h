// Checksums.h

#ifndef CHECKSUMS_H
#define CHECKSUMS_H

#include <cstdint>
#include <Functions.h>

enum class HeaderType : uint8_t;

/**
 * @namespace Checksum
 * @brief Provides functions for checksum calculations and related utilities.
 *
 * The Checksum namespace encapsulates various functions used to calculate different types of checksums,
 * convert data formats, and handle protocol-specific checksum operations.
 */
namespace Checksum
{

    /**
     * @brief Calculates the CRC32 checksum of the given data.
     *
     * This function computes the CRC32 checksum for the provided ByteString data using a predefined CRC32 table.
     *
     * @param data The ByteString data for which to calculate the CRC32 checksum.
     * @return ByteString A 4-byte ByteString representing the CRC32 checksum.
     *
     * @note The CRC32 checksum is commonly used for error-checking in data transmission.
     */
    bool crc32(uint8_t* out, const uint8_t* data, size_t dataSize);

    /**
     * @brief Inserts a calculated checksum into a specific location within a header.
     *
     * @param pseudoHeader Optional pseudo-header ByteString to include in the checksum calculation.
     * @param header The header ByteString where the checksum will be inserted.
     * @param checksumStartIndex The byte index in the header where the checksum should be placed.
     * @param checksumSize The size (in bytes) of the checksum.
     * @param swap Optional flag indicating whether to swap the byte order of the checksum before insertion.
     * @return A new ByteString representing the header with the checksum inserted.
     */
    void calculateChecksum(uint8_t* packet, size_t headerSize, size_t checksumStartIndex, size_t checksumSize, const uint8_t* pseudoHeader = nullptr, size_t pseudoHeaderSize = 0, bool swap = false);
}

#endif // CHECKSUMS_H
