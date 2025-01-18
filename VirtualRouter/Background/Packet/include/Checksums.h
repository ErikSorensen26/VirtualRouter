// Checksums.h

#ifndef CHECKSUMS_H
#define CHECKSUMS_H

#include <cstdint>
#include <vector>
#include <string>
#include <Functions.h>
#include <ByteString.hpp>
#include <array>

enum class HeaderType;

/**
 * @namespace Checksum
 * @brief Provides functions for checksum calculations and related utilities.
 *
 * The Checksum namespace encapsulates various functions used to calculate different types of checksums,
 * convert data formats, and handle protocol-specific checksum operations.
 */
namespace Checksum
{
    std::vector<uint8_t> base256StringToBytes(const std::string &base256Str);
    /**
     * @brief Converts a base256-encoded string to a vector of bytes.
     *
     * This function takes a base256-encoded string and converts it into a vector of uint8_t bytes.
     *
     * @param base256Str The base256-encoded string to convert.
     * @return std::vector<uint8_t> A vector containing the byte representation of the input string.
     *
     * @note Ensure that the input string is properly base256-encoded to avoid unexpected results.
     */
    std::vector<uint8_t> base256StringToBytes(const ByteString base256Str);

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
    ByteString crc32(const ByteString &data);

    /**
     * @brief Calculates a checksum over the given data.
     *
     * @param data The input ByteString containing data bytes.
     * @param checksumSizeBytes The size of the checksum in bytes (e.g., 1, 2, or 4).
     * @return ByteString containing the checksum bytes. If checksumSizeBytes is unsupported,
     *         returns a ByteString filled with zeros of the specified size.
     */
    ByteString calculateChecksum(const ByteString& data, size_t checksumSizeBytes);

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
    void calculateProtocolChecksum(const ByteString pseudoHeader, ByteString& header, size_t checksumStartIndex, size_t checksumSize, bool swap = false);

    /**
     * @brief Calculates a checksum over a range of headers and inserts it into a designated header.
     *
     * @param headers A vector of headers indexed by HeaderType enum.
     * @param pseudoHeader Optional pseudo-header ByteString to include in the checksum calculation.
     * @param payload The payload ByteString to include in the checksum calculation.
     * @param checksumHeaderType The HeaderType enum value indicating which header the checksum should be inserted into.
     * @param checksumStartIndex The byte index in the checksum header where the checksum should be placed.
     * @param startHeaderType The HeaderType enum value indicating where to start the checksum calculation.
     * @param checksumSize The size (in bytes) of the checksum.
     * @param swap Optional flag indicating whether to swap the byte order of the checksum before insertion.
     * @return An optional ByteString containing the final packet with the checksum inserted, or std::nullopt on error.
     */
    void calculateProtocolChecksum(std::optional<ByteString>(&headers)[], const ByteString& pseudoHeader, const ByteString& payload, HeaderType startHeaderType, size_t checksumStartIndex, size_t checksumSize, bool swap = false);
}

#endif // CHECKSUMS_H
