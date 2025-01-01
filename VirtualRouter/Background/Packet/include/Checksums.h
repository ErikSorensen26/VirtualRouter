#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <Functions.h>
#include <ByteString.hpp>

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
     * @brief Calculates a generic 16-bit checksum over the given data.
     *
     * This function computes a simple checksum by summing all 16-bit words in the data.
     * If the data length is odd, it pads the last byte with zero.
     *
     * @param data Pointer to the data buffer consisting of 16-bit words.
     * @param length The length of the data in bytes.
     * @return std::string A hexadecimal string representation of the calculated checksum.
     */
    std::string calculateChecksum(const uint16_t *data, size_t length);

    /**
     * @brief Calculates a protocol-specific checksum and updates the given data string.
     *
     * This function prepares and calculates a checksum for a specific protocol header.
     * It converts the input string to bytes, calculates the checksum, optionally swaps bytes,
     * and replaces the checksum in the original data string.
     *
     * @param data_str The original ByteString data containing the protocol header.
     * @param startIndex The starting index in the data string where the checksum is located.
     * @param headerlength The length of the protocol header.
     * @param index The position within the header where the checksum should be inserted.
     * @param swap (Optional) Boolean flag indicating whether to swap bytes in the checksum. Defaults to false.
     * @return ByteString The updated ByteString with the calculated checksum inserted.
     *
     * @note Ensure that the startIndex and index parameters correctly reference the checksum location within the data string.
     */
    ByteString calculateProtocolChecksum(const ByteString &data_str, int startIndex, int headerlength, int index, bool swap = false);
}
