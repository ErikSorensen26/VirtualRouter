#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <Functions.h>
#include <ByteString.hpp>

namespace Checksum
{
    // Converts byte to string
    std::vector<uint8_t> base256StringToBytes(const ByteString base256Str);

    // Calculates crc32 hash
    ByteString crc32(const ByteString &data);

    // Calculates checksum
    std::string calculateChecksum(const uint16_t *data, size_t length);

    // Prepares and calculates checksum for specific header
    ByteString calculateProtocolChecksum(const ByteString &data_str, int startIndex, int headerlength, int index, bool swap = false);
}
