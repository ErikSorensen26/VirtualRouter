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

double secondsSinceEpoch();

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
     * @brief Converts a string of bytes to a 128bit integer.
     * @param str The input byte string.
     * @return The resulting integer.
     */
    __uint128_t byteToNum128(const ByteString& str);

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
     * @brief Converts an 128 integer to a byte string.
     * @param num The input integer.
     * @return A byte string representation.
     */
    ByteString numToByte128(__uint128_t bytes);

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
     * @brief Seperates the network address from the mask from a signle string.
     * @param maskAddress Address with the included mask.
     * @param address Address that the seperated address will be set to.
     * @param mask Prefix mask that the mask will be set to.
     */
    bool splitSlashMiddle(const std::string& maskAddress, ByteString& address, uint8_t& mask);

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
    ByteString findClassfullNetwork(const ByteString& ip);

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

    /**
     * @brief Expands an abbreviated IPv6 address to its full form.
     *
     * Converts a compressed IPv6 address containing "::" into its fully expanded form with all eight hextets,
     * padding each segment with leading zeros as necessary.
     *
     * @param ipv6Address The compressed IPv6 address to expand.
     * @return std::string The fully expanded IPv6 address.
     */
    std::string expandIPv6Address(const std::string& ipv6Address);

    /**
     * @brief Validates if a string is a valid IPv6 address.
     *
     * Uses a regular expression to check if the provided string conforms to IPv6 address standards.
     *
     * @param address The IPv6 address string to validate.
     * @return true If the address is a valid IPv6 format; otherwise, false.
     */
    bool isIPv6Address(const std::string& address);

    /**
     * @brief Validates if a string is a valid IPv6 address with a subnet mask.
     *
     * Uses a regular expression to check if the provided string conforms to IPv6 address standards with an appended subnet mask.
     *
     * @param addressWithMask The IPv6 address string with subnet mask to validate.
     * @return true If the address with mask is valid; otherwise, false.
     */
    bool isIPv6AddressWithMask(const std::string& addressWithMask);

     /**
     * @brief Validates if a string is a valid MAC address.
     *
     * Uses a regular expression to check if the provided string conforms to MAC address standards,
     * allowing for different separators such as colons or hyphens.
     *
     * @param macAddress The MAC address string to validate.
     * @return true If the MAC address is valid; otherwise, false.
     */
    bool isMACAddress(const std::string& macAddress);

    /**
     * @brief Validates if an IPv6 address is a local link address.
     *
     * Takes in a user typed IP address and validates it as a local-link address.
     *
     * @param input IPv6 local-link address.
     * @return bool Indicates if the ipv6 address is a valid local-link address.
     */
    bool isLocalLink(const ByteString& input);

    /**
     * @brief Validates if an IPv6 address is a global unicast address.
     *
     * Takes in a user typed IP address and validates it as a global unicast address.
     *
     * @param input IPv6 global unicast address.
     * @return bool Indicates if the ipv6 address is a valid global unicast address.
     */
    bool isGlobalUnicast(const ByteString& input);

    /**
     * @brief Validates if an IPv6 adderess is a unicast local address
     *
     * Takes in a user typed IP addres and validates it as a unicast local address.
     *
     * @param input IPv6 unicast local address.
     * @return bool Indicates if the ipv6 address is a valid global unicast address.
     */
    bool isLocalUnicast(const ByteString& input);

    /**
     * @brief Splits a string into tokens based on a delimiter.
     *
     * Splits the input string into tokens using the provided delimiter character and returns the resulting vector of tokens.
     *
     * @param input The input string to tokenize.
     * @param delimiter The character used to delimit tokens in the input string.
     * @return std::vector<std::string> A vector containing the individual tokens extracted from the input.
     */
    std::vector<std::string> tokenize(const std::string& input, char delimiter);

    /**
     * @brief Pads a string with leading zeros (e.g., for IPv6 segments).
     *
     * Formats the input string by adding leading zeros until it reaches a width of four characters.
     *
     * @param input The input string to pad with zeros.
     * @return std::string The zero-padded string.
     */
    std::string padWithZeros(const std::string& input);

    std::string generateRandomString(size_t length);

    std::optional<std::pair<std::string, std::string>> splitMiddle(const std::string& full, char delimiter);
}

#endif // FUNCTIONS_H
