// Functions.h

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <netinet/in.h>
#include <optional>
#include <ctime>
#include <chrono>
#include <AddressFamily.hpp>
#include <IPAddress.hpp>

//static bool validateSize(size_t& beginning, size_t length, const ByteString& packet)
//{
    //return (beginning + length <= packet.size());
//}

double secondsSinceEpoch();

namespace Functions {
    std::string boolToString(bool bol);
    std::string lowerCase(std::string str);
    IPAddress getAddress(const std::string& address);
    uint8_t prefixToPrefixLength(uint32_t mask);
    uint32_t addressToIntv4(const std::string& address);
    __uint128_t addressToIntv6(const std::string& address);
    uint64_t macToInt(const std::string& mac);
    bool isNumber(const std::string& s);
    bool isHex(const std::string& s);

    /**
     * @brief Seperates the network address from the mask from a signle string.
     * @param maskAddress Address with the included mask.
     * @param address Address that the seperated address will be set to.
     * @param mask Prefix mask that the mask will be set to.
     */
    bool splitSlashMiddle(const std::string& maskAddress, IPAddress& address, uint8_t& mask);

    /**
     * @brief Computes the network address from an IP address and a subnet mask.
     * @param ipAddress The input IP address.
     * @param mask The subnet mask in bits.
     * @return The computed network address.
     */
    uint8_t* computeNetworkAddress(uint8_t* out, const uint8_t* ipAddress, uint8_t mask, AddressFamily af);

    /**
     * @brief Converts a byte-based IP address to its dot-decimal notation.
     * @param ip The input byte-based IP address.
     * @return The dot-decimal representation of the IP address.
     */
    std::string byteAddressToNumAddress(const uint8_t* ip);

    /**
     * @brief Compares a network address with an IP address to check if they match.
     * @param networkAddress The network address.
     * @param ipAddress The IP address.
     * @param mask The subnet mask in bits.
     * @return True if they match, false otherwise.
     */
    bool compareNetworkWithIp(const uint8_t* networkAddress, const uint8_t* ipAddress, uint8_t mask, AddressFamily af);

    /**
     * @brief Trims the network address based on the subnet mask, removing zeroed sections.
     * @param network The network address.
     * @param mask The subnet mask in bits.
     */
    size_t compactNetworkAddress(uint8_t* out, const uint8_t* network, uint8_t mask, AddressFamily af);

    /**
     * @brief Generates a random integer between min and max (inclusive).
     * @param min The minimum value.
     * @param max The maximum value.
     * @return A random integer in the specified range.
     */
    size_t getRandomBetween(size_t min, size_t max);

    /**
     * @brief Calculates an EUI-64 address.
     *
     * @param prefix The prefix the address is being created from.
     * @param mac The MAC address.
     * @param prefixLen The prefix length of the network.
     * @return The calculated EUI-64 address, or nullopt if calculation fails.
     */
    uint8_t* calculateEui64(uint8_t* out, const uint8_t* prefix, const uint8_t* mac);

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
    bool compareNetworkWithMask(const uint8_t* network, uint8_t mask, AddressFamily af);

    /**
     * @brief Checks if a subnet (network/mask) is within a summary (summaryNetwork/summaryMask).
     * @param network The subnet network address.
     * @param mask The subnet mask in bits.
     * @param summaryNetwork The summary network address.
     * @param summaryMask The summary mask in bits.
     * @return True if the subnet is within the summary, false otherwise.
     */
    bool isSubnetOf(const uint8_t* network, uint8_t mask, const uint8_t* summaryNetwork, uint8_t summaryMask, AddressFamily af);

    /**
     * @brief Finds the classful network address for an IP address.
     * @param ip The input IP address.
     * @return The classful network address.
     */
    uint32_t findClassfullNetwork(uint32_t ip);

    uint8_t findClassfullNetworkAndMask(uint8_t* out, const uint8_t* ip);

    /**
     * @brief Gets the default subnet mask for a network.
     * @param network The input network address.
     * @return The default subnet mask in bits.
     */
    uint8_t getDefaultMask(uint32_t network);

    /**
     * @brief Validates a MAC address.
     * @param mac The input MAC address.
     * @param currentMac The current MAC address to compare against.
     * @return True if the MAC address is valid, false otherwise.
     */
    bool validateMacAddress(const uint8_t* mac, const uint8_t* currentMac);

    /**
     * @brief Determines if an IP address is multicast.
     * @param ip The input IP address.
     * @return True if the IP address is multicast, false otherwise.
     */
    bool isMulticast(const uint8_t* ip, AddressFamily af);

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
    bool isLocalLink(const uint8_t* input);
    bool isLocalLink(__uint128_t input);

    /**
     * @brief Validates if an IPv6 address is a global unicast address.
     *
     * Takes in a user typed IP address and validates it as a global unicast address.
     *
     * @param input IPv6 global unicast address.
     * @return bool Indicates if the ipv6 address is a valid global unicast address.
     */
    bool isGlobalUnicast(const uint8_t* input);
    bool isGlobalUnicast(__uint128_t input);

    /**
     * @brief Validates if an IPv6 adderess is a unicast local address
     *
     * Takes in a user typed IP addres and validates it as a unicast local address.
     *
     * @param input IPv6 unicast local address.
     * @return bool Indicates if the ipv6 address is a valid global unicast address.
     */
    bool isLocalUnicast(const uint8_t* input);
    bool isLocalUnicast(__uint128_t input);

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

    uint8_t* prefixToMask(uint8_t* out, uint8_t prefixLen, AddressFamily af);

    std::string generateRandomString(size_t length);

    std::optional<std::pair<std::string, std::string>> splitMiddle(const std::string& full, char delimiter);

    bool isBitSet(const uint8_t byte, uint8_t bitPosition);
}

#endif // FUNCTIONS_H
