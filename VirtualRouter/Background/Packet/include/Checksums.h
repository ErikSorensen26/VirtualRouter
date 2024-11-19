#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <Functions.h>

using namespace std;

namespace Checksum
{
    // Converts byte to string
    std::vector<uint8_t> base256StringToBytes(const std::string base256Str);

    // Calculates crc32 hash
    std::string Crc32(const std::string &data);

    // Calculates checksum
    std::string CalculateChecksum(const uint16_t *data, size_t length);

    // Prepares and calculates checksum for specific header
    std::string CalculateProtocolChecksum(const std::string &data_str, int startIndex, int headerlength, int index, bool swap = false);
}