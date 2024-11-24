#pragma once

#include <iostream>
#include <string>
#include <bitset>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstdint>
#include <random>
#include <netinet/in.h>
#include <algorithm>
#include <optional>
#include <regex>
#include <ctime>
#include <chrono>

namespace Functions {
    // Adjusts the size of a string by prepending `value` until it reaches `size`.
    std::string changeSize(std::string str, size_t size, std::string value = std::string("\x00", 1));

    // Converts a single byte to a hexadecimal string representation.
    std::string charToHex(unsigned char byte);

    // Converts a hexadecimal string to an unsigned integer.
    unsigned hexToNum(const std::string& hexStr);

    // Converts a single byte to a binary string representation (8 bits).
    std::string charToBin(unsigned char byte);

    // Checks if a string contains only binary characters ('0' and '1').
    bool isBinary(const std::string& str);

    // Checks if a string contains only hexadecimal characters (0-9, A-F).
    bool isHex(const std::string& str);

    // Checks if a string contains only decimal charectors (0-9).
    bool isDecimal(const std::string& str);

    // Converts a string of bytes to a hexadecimal string representation.
    std::string byteToHex(const std::string& input);

    // Converts a string of bytes to a binary string representation.
    std::string byteToBin(const std::string& input);

    // Converts a vector of bytes to a hexadecimal string representation.
    std::string byteArrayToHex(const std::vector<uint8_t>& byte_array);

    // Converts a string of bytes to an integer, treating the bytes as hexadecimal values.
    int byteToNum(std::string str);

    // Converts a binary string to a hexadecimal string representation.
    std::string binToHex(const std::string& binaryStr);

    // Converts a single hexadecimal character to its 4-bit binary representation.
    std::string hexDigitToBin(char hexDigit);

    // Converts a hexadecimal string to a binary string representation.
    std::string hexToBin(const std::string& hexStr);

    // Converts a hexadecimal string to a byte string. If `size` is specified, adjusts the result to the given size.
    std::string hexToByte(const std::string& hexBinaryData, size_t size = 0);

    // Converts a boolean to a string representation ("1" for true, "0" for false).
    std::string boolToString(bool bol);

    // Converts a binary string to a byte string. If `size` is specified, adjusts the result to the given size.
    std::string binToByte(const std::string& binaryData, size_t size = 0);

    // Converts a binary string to an integer.
    uint8_t binToNum(const std::string& binary);

    // Converts a string to lowercase.
    std::string lowerCase(std::string str);

    // Converts a string to a boolean. Returns true if the string is "1", false otherwise.
    bool stringToBool(const std::string& str);

    // Converts a string to an integer.
    int stringToNum(std::string num);

    // Converts an integer to a hexadecimal string, ensuring at least two characters.
    std::string numToHex(int num, int size = 0);

    // Converts an integer to a hexadecimal string of a specific byte size.
    std::string numToHexWithByte(uint32_t num, int byteSize);

    // Converts an integer to a byte string. If `size` is specified, adjusts the result to the given size.
    std::string numToByte(int num, size_t size = 0);

    // Converts an IPv4 address in dot-decimal notation to a byte string.
    std::string addressToByte(const std::string& mask);

    // Prints each element in a vector of strings.
    void printVector(const std::vector<std::string>& vec);

    // Counts the number of '1' bits in a binary representation of a mask string.
    int byteMaskToNum(const std::string& mask);

    // Creates a binary string representation of a network mask with a specified number of bits.
    std::string numMaskToBin(int mask);

    // Computes the network address from an IP address and a subnet mask.
    std::string computeNetworkAddress(const std::string& ipAddress, int mask);

    // Converts a byte-based IP address to its dot-decimal notation.
    std::string byteAddressToNumAddress(const std::string& ip);

    // Compares a network address with an IP address to check if they match.
    bool compareNetworkWithIp(std::string networkAddress, std::string ipAddress);

    // Trims the network address based on the subnet mask, removing zeroed sections.
    std::string compactNetworkAddress(std::string network, int mask);

    // Generates a random integer between min and max (inclusive).
    int getRandomBetween(int min, int max);

    // Reverses a binary string, flipping '0' to '1' and '1' to '0'.
    std::string reverseBinary(const std::string& binary);

    // Calculated EUI-64 address
    std::optional<std::string> calculateEui64(std::string mac, std::string fullIPv6);

    // Converts time point to string
    std::string timeToString(const std::chrono::system_clock::time_point time);

    // Tests network addres against subnet mask
    bool compareNetworkWithMask(const std::string& network, int mask);

    // Checks if subnet (network/mask) is within summary (summaryNetwork/summaryMask)
    bool isSubnetOf(const std::string& network, int mask, const std::string& summaryNetwork, int summaryMask);

    // Finds classfull network address
    std::string findClassfullNetwork(std::string& ip);

    // Gets default mask for a network
    int getDefaultMask(const std::string& network);
}
