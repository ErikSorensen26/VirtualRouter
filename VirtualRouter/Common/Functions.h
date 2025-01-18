// Functions.h

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <string>
#include <ByteString.hpp>
#include <vector>
#include <cmath>
#include <cstdint>
#include <netinet/in.h>
#include <optional>
#include <ctime>
#include <chrono>

namespace Functions {
    /**
     * @brief Adjusts the suze of a string by prepending 'value' until it reaches 'size'.
     * @param str The input string.
     * @param size The target size.
     * @param value The value to prepend (default is null byte).
     * @return A string adjusted to the target size.
     */
    ByteString changeSize(ByteString str, size_t size, ByteString value = ByteString("\x00", 1));

    /**
     * @brief Converts a single byte to a hexadecimal string representation.
     * @param byte The input byte.
     * @return A two-character hexadecimal string.
     */
    ByteString charToHex(uint8_t byte);

    /**
     * @brief Converts a hexadecimal string to an unsigned integer.
     * @param hexStr The input hexadecimal string.
     * @return The resulting unsigned integer.
     */
    uint32_t hexToNum(const ByteString& hexStr);

    /**
     * @brief Converts a single byte to a binary string representation (8 bits).
     * @param byte The input byte.
     * @return A binary string representation of the byte.
     */
    ByteString charToBin(uint8_t byte);

    /**
     * @brief Checks if a string contains only binary characters ('0' and '1').
     * @param str The input string.
     * @return True if the string is binary, false otherwise.
     */
    bool isBinary(const ByteString& str);

    /**
     * @brief Checks if a string contains only hexadecimal characters (0-9, A-F).
     * @param str The input string.
     * @return True if the string is hexadecimal, false otherwise.
     */
    bool isHex(const ByteString& str);

    /**
     * @brief Checks if a string contains only decimal characters (0-9).
     * @param str The input string.
     * @return True if the string is decimal, false otherwise.
     */
    bool isDecimal(const std::string& str);

    /**
     * @brief Converts a string of bytes to a hexadecimal string representation.
     * @param input The input byte string.
     * @return A hexadecimal string representation.
     */
    ByteString byteToHex(const ByteString& input);

    /**
     * @brief Converts a string of bytes to a binary string representation.
     * @param input The input byte string.
     * @return A binary string representation.
     */
    ByteString byteToBin(const ByteString& input);

    /**
     * @brief Converts a vector of bytes to a hexadecimal string representation.
     * @param byte_array The input vector of bytes.
     * @return A hexadecimal string representation.
     */
    ByteString byteArrayToHex(const std::vector<uint8_t>& byte_array);

    /**
     * @brief Converts a string of bytes to an integer, treating the bytes as hexadecimal values.
     * @param str The input byte string.
     * @return The resulting integer.
     */
    uint32_t byteToNum(ByteString str);

    /**
     * @brief Converts a binary string to a hexadecimal string representation.
     * @param binaryStr The input binary string.
     * @return A hexadecimal string representation.
     */
    ByteString binToHex(const ByteString& binaryStr);

    /**
     * @brief Converts a single hexadecimal character to its 4-bit binary representation.
     * @param hexDigit The input hexadecimal character.
     * @return A binary string representation.
     */
    ByteString hexDigitToBin(uint8_t hexDigit);

    /**
     * @brief Converts a hexadecimal string to a binary string representation.
     * @param hexStr The input hexadecimal string.
     * @return A binary string representation.
     */
    ByteString hexToBin(const ByteString& hexStr);

    /**
     * @brief Converts a hexadecimal string to a byte string. Adjusts to the given size if specified.
     * @param hexBinaryData The input hexadecimal string.
     * @param size The desired size of the output (default is 0, meaning no adjustment).
     * @return A byte string representation.
     */
    ByteString hexToByte(const ByteString& hexBinaryData, size_t size = 0);

    /**
     * @brief Converts a boolean to a string representation ("1" for true, "0" for false).
     * @param bol The input boolean value.
     * @return A string representation of the boolean.
     */
    std::string boolToString(bool bol);

    /**
     * @brief Converts a binary string to a byte string. Adjusts to the given size if specified.
     * @param binaryData The input binary string.
     * @param size The desired size of the output (default is 0, meaning no adjustment).
     * @return A byte string representation.
     */
    ByteString binToByte(const ByteString& binaryData, size_t size = 0);

    /**
     * @brief Converts a binary string to an integer.
     * @param binary The input binary string.
     * @return The resulting integer.
     */
    uint32_t binToNum(const ByteString& binary);

    /**
     * @brief Converts a string to lowercase.
     * @param str The input string.
     * @return The lowercase version of the string.
     */
    std::string lowerCase(std::string str);

    /**
     * @brief Converts a string to a boolean. Returns true if the string is "1", false otherwise.
     * @param str The input string.
     * @return The boolean value.
     */
    bool stringToBool(const std::string& str);

    /**
     * @brief Converts a string to an integer.
     * @param num The input string.
     * @return The resulting integer.
     */
    uint32_t stringToNum(std::string num);

    /**
     * @brief Converts an integer to a hexadecimal string, ensuring at least two characters.
     * @param num The input integer.
     * @param size The desired size of the output (default is 0, meaning no adjustment).
     * @return A hexadecimal string representation.
     */
    ByteString numToHex(size_t num, size_t size = 0);

    /**
     * @brief Converts an integer to a binary string.
     * @param number The input integer.
     * @param size The desired size of the output.
     * @return A binary string representation.
     */
    ByteString numToBin(size_t number, size_t length);

    /**
     * @brief Converts an integer to a hexadecimal string of a specific byte size.
     * @param num The input integer.
     * @param byteSize The desired byte size of the output.
     * @return A hexadecimal string representation.
     */
    ByteString numToHexWithByte(uint32_t num, size_t byteSize);

    /**
     * @brief Converts an integer to a byte string. Adjusts to the given size if specified.
     * @param num The input integer.
     * @param size The desired size of the output (default is 0, meaning no adjustment).
     * @return A byte string representation.
     */
    ByteString numToByte(size_t num, size_t size = 0);

    /**
     * @brief Converts an IPv4 address in dot-decimal notation to a byte string.
     * @param mask The input IPv4 address in dot-decimal notation.
     * @return A byte string representation of the address.
     */
    ByteString addressToByte(const ByteString& mask);

    /**
     * @brief Prints each element in a vector of strings.
     * @param vec The input vector of strings.
     */
    void printVector(const std::vector<std::string>& vec);

    /**
     * @brief Counts the number of '1' bits in a binary representation of a mask string.
     * @param mask The input binary mask string.
     * @return The number of '1' bits.
     */
    uint8_t byteMaskToNum(const ByteString& mask);

    /**
     * @brief Creates a binary string representation of a network mask with a specified number of bits.
     * @param mask The number of bits in the mask.
     * @return A binary string representation of the mask.
     */
    ByteString numMaskToBin(uint8_t mask);

    /**
     * @brief Computes the network address from an IP address and a subnet mask.
     * @param ipAddress The input IP address.
     * @param mask The subnet mask in bits.
     * @return The computed network address.
     */
    ByteString computeNetworkAddress(const ByteString& ipAddress, uint8_t mask);

    /**
     * @brief Converts a byte-based IP address to its dot-decimal notation.
     * @param ip The input byte-based IP address.
     * @return The dot-decimal representation of the IP address.
     */
    ByteString byteAddressToNumAddress(const ByteString& ip);

    /**
     * @brief Compares a network address with an IP address to check if they match.
     * @param networkAddress The network address.
     * @param ipAddress The IP address.
     * @param mask The subnet mask in bits.
     * @return True if they match, false otherwise.
     */
    bool compareNetworkWithIp(ByteString networkAddress, ByteString ipAddress, uint8_t mask);

    /**
     * @brief Trims the network address based on the subnet mask, removing zeroed sections.
     * @param network The network address.
     * @param mask The subnet mask in bits.
     * @return The compacted network address.
     */
    ByteString compactNetworkAddress(ByteString network, uint8_t mask);

    /**
     * @brief Generates a random integer between min and max (inclusive).
     * @param min The minimum value.
     * @param max The maximum value.
     * @return A random integer in the specified range.
     */
    size_t getRandomBetween(size_t min, size_t max);

    /**
     * @brief Reverses a binary string, flipping '0' to '1' and '1' to '0'.
     * @param binary The input binary string.
     * @return The reversed binary string.
     */
    ByteString reverseBinary(const ByteString& binary);

    /**
     * @brief Calculates an EUI-64 address.
     * @param mac The MAC address.
     * @param fullIPv6 The full IPv6 address.
     * @return The calculated EUI-64 address, or nullopt if calculation fails.
     */
    std::optional<ByteString> calculateEui64(ByteString mac, ByteString fullIPv6);

    /**
     * @brief Converts a time point to a string.
     * @param time The time point to convert.
     * @return The string representation of the time.
     */
    std::string timeToString(const std::chrono::system_clock::time_point time);

    /**
     * @brief Tests if a network address matches a subnet mask.
     * @param network The network address.
     * @param mask The subnet mask in bits.
     * @return True if the address matches the mask, false otherwise.
     */
    bool compareNetworkWithMask(const ByteString& network, uint8_t mask);

    /**
     * @brief Checks if a subnet (network/mask) is within a summary (summaryNetwork/summaryMask).
     * @param network The subnet network address.
     * @param mask The subnet mask in bits.
     * @param summaryNetwork The summary network address.
     * @param summaryMask The summary mask in bits.
     * @return True if the subnet is within the summary, false otherwise.
     */
    bool isSubnetOf(const ByteString& network, uint8_t mask, const ByteString& summaryNetwork, uint8_t summaryMask);

    /**
     * @brief Finds the classful network address for an IP address.
     * @param ip The input IP address.
     * @return The classful network address.
     */
    ByteString findClassfullNetwork(ByteString& ip);

    /**
     * @brief Gets the default subnet mask for a network.
     * @param network The input network address.
     * @return The default subnet mask in bits.
     */
    uint8_t getDefaultMask(const ByteString& network);

    /**
     * @brief Validates a MAC address.
     * @param mac The input MAC address.
     * @param currentMac The current MAC address to compare against.
     * @return True if the MAC address is valid, false otherwise.
     */
    bool validateMacAddress(const ByteString& mac, const ByteString currentMac);

    /**
     * @brief Determines if an IP address is multicast.
     * @param ip The input IP address.
     * @return True if the IP address is multicast, false otherwise.
     */
    bool isMulticast(const ByteString& ip);
}

#endif // FUNCTIONS_H
