#include "Functions.h"
#include <Logger.h>
#include <bitset>
#include <random>
#include <regex>

namespace Functions {

    #pragma region TestIf

    bool isBinary(const ByteString& str) 
    {
        return !str.empty() && std::all_of(str.begin(), str.end(), [](char c) {return c == '0' || c == '1';});
    }

    bool isHex(const ByteString& str) 
    {
        return !str.empty() && std::all_of(str.begin(), str.end(), ::isxdigit);
    }

    bool isDecimal(const std::string& str)
    {
        return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
    }

    #pragma endregion
    #pragma region ByteConv

    ByteString byteToHex(const ByteString& input) 
    {
        if (input.empty()) return "";
        std::ostringstream ss;
        ss << std::hex << std::uppercase;
        for (unsigned char byte : input)
        {
            ss << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return ss.str();
    }

    ByteString byteToBin(const ByteString& input) 
    {
        if (input.empty()) return "";
        ByteString binaryStr;
        binaryStr.reserve(input.size() * 8);
        for (unsigned char byte : input)
        {
            binaryStr += std::bitset<8>(byte).to_string();
        }
        return binaryStr;
    }

    ByteString byteArrayToHex(const std::vector<uint8_t>& byte_array) 
    {
        if (byte_array.empty()) return "";
        std::ostringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0');
        for (uint8_t byte : byte_array)
        {
            ss << std::setw(2) << static_cast<int>(byte);
        }
        return ss.str();
    }

    uint32_t byteToNum(ByteString str) 
    {
        return hexToNum(byteToHex(str));
    }

    #pragma endregion
    #pragma region HexConv

    ByteString binToHex(const ByteString& binaryStr) 
    {
        if (binaryStr.empty()) {return "";}
        std::stringstream ss;
        ByteString paddedBinaryStr = binaryStr;
        uint8_t padLength = 4 - (binaryStr.size() % 4);
        if (padLength != 4) {
            paddedBinaryStr = ByteString(padLength, '0') + binaryStr;
        }
        for (size_t i = 0; i < paddedBinaryStr.size(); i += 4) {
            ByteString fourBits = paddedBinaryStr.substr(i, 4);
            ss << std::hex << std::uppercase << std::bitset<4>(fourBits.toString()).to_ulong();
        }
        return ss.str();
    }

    ByteString hexDigitToBin(uint8_t hexDigit) 
    {
        static const char* lookup[] = {
            "0000","0001","0010","0011","0100","0101","0110","0111",
            "1000","1001","1010","1011","1100","1101","1110","1111"
        };
        if (hexDigit >= '0' && hexDigit <= '9') return lookup[hexDigit - '0'];
        if (hexDigit >= 'A' && hexDigit <= 'F') return lookup[hexDigit - 'A' + 10];
        if (hexDigit >= 'a' && hexDigit <= 'f') return lookup[hexDigit - 'a' + 10];
        return "";
    }

    ByteString hexToBin(const ByteString& hexStr) {
        if (hexStr.empty()) {return "";}
        ByteString binaryStr;
        binaryStr.reserve(hexStr.size() * 4);
        for (char hexDigit : hexStr) {
            ByteString binStr = hexDigitToBin(hexDigit);
            if (binStr.empty()) {
                std::cerr << "Invalid hexadecimal digit: " << hexDigit << std::endl;
                return "";
            }
            binaryStr += binStr;
        }
        return binaryStr;
    }

    ByteString hexToByte(const ByteString& hexBinaryData, size_t size) {
        if (hexBinaryData.empty()) return "";
        size_t length = hexBinaryData.size();
        ByteString byteString;
        byteString.reserve((length + 1) / 2);

        std::string temp = hexBinaryData.toString();
        if (length % 2 != 0) {
            byteString = ByteString("0") + byteString;
            length = hexBinaryData.size();
        }

        for (size_t i = 0; i < length; i += 2) {
            unsigned int byte;
            if (sscanf(temp.c_str() + i, "%2x", &byte) != 1)
            {
                std::cerr << "Invalid hex byte: " << temp.substr(i, 2) << std::endl;
                return "";
            }
            byteString.push_back(static_cast<unsigned char>(byte));
        }

        if (size != 0)
        {
            byteString = changeSize(byteString, size, ByteString("\x00", 1));
        }

        return byteString;
    }

    uint32_t hexToNum(const ByteString& hexStr) 
    {
        char* endptr;
        return strtol(hexStr.toString().c_str(), &endptr, 16);
    }

    #pragma endregion
    #pragma region BoolConv

    std::string boolToString(bool bol) 
    {
        return bol ? "1" : "0";
    }

    #pragma endregion
    #pragma region BinConv

    ByteString binToByte(const ByteString& binaryData, size_t size) 
    {
        return hexToByte(binToHex(binaryData), size);
    }

    uint32_t binToNum(const ByteString& binary) 
    {
        double decimal = 0;
        size_t length = binary.size();
        for (size_t i = 0; i < length; ++i) {
            if (binary[length - 1 - i] == '1') {
                decimal += (1U << i);
            }
        }
        return static_cast<unsigned int>(decimal);
    }

    #pragma endregion
    #pragma region StringConv

    std::string lowerCase(std::string str) 
    {
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) -> unsigned char {
            return static_cast<unsigned char>(std::tolower(c));
        });
        return str;
    }

    bool stringToBool(const std::string& str) 
    {
        return str == "1";
    }

    uint32_t stringToNum(std::string num) 
    {
        if (Functions::isDecimal(num))
        {
            return static_cast<uint32_t>(std::stoul(num, nullptr, 10));
        }
        return 0;
    }

    #pragma endregion
    #pragma region NumConv

    ByteString numToBin(size_t number, size_t length)
    {
        std::bitset<64> binary(number);
        std::string binaryStr = binary.to_string();
        if (length > 64) length = 64;
        return binaryStr.substr(64 - length);
    }

    #pragma endregion
    #pragma region CharConv

    ByteString charToHex(uint8_t byte) 
    {
        std::stringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        return ss.str();
    }

    ByteString charToBin(uint8_t byte) 
    {
        return std::bitset<8>(byte).to_string();
    }

    #pragma endregion
    #pragma region NumChar

    ByteString numToHex(size_t num, size_t size) 
    {
        std::ostringstream ss;
        ss << std::hex << std::uppercase;
        ss << std::setw(static_cast<int>(size != 0 ? size : 2)) << std::setfill('0') << num;
        ByteString hex = ss.str();
        if (size == 0 && hex.size() % 2 != 0)
        {
            hex = ByteString("0") + hex;
        }
        return hex;
    }

    ByteString numToHexWithByte(uint32_t num, int byteSize) 
    {
        if (byteSize <= 0) {
            throw std::invalid_argument("byteSize must be positive");
        }

        uint32_t maxVal = (byteSize >= 4) ? 0xFFFFFFFF :
                          (byteSize >= 3) ? 0xFFFFFF :
                          (byteSize >= 2) ? 0xFFFF :
                          (byteSize >= 1) ? 0xFF : 0;
        
        if (num > maxVal)
        {
            throw std::out_of_range("Number exceeds the maximun value for the specified byte size");
        }

        uint32_t networkOrderNum = 0;
        switch (byteSize)
        {
            case 1:
                networkOrderNum = num & 0xFF;
                break;
            case 2:
                networkOrderNum = htons(static_cast<uint16_t>(num));
                break;
            case 4:
                networkOrderNum = htonl(num);
                break;
            default:
                throw std::invalid_argument("Unsupported byte size. Supported sizes: 1, 2, 4");
        }

        std::ostringstream ss;
        ss << std::hex << std::uppercase << std::setw(byteSize * 2) << std::setfill('0') << networkOrderNum;
        return ss.str();
    }

    ByteString numToByte(size_t num, size_t size) 
    {
        return hexToByte(numToHex(num), size);
    }

    #pragma endregion
    #pragma region NetConv

    ByteString addressToByte(const ByteString& address)
    {
        ByteString byteAddress;

        // Check for ipv4
        std::regex ipv4Range("^([0-9]{1,3}\\.){3}[0-9]{1,3}$");
        if (std::regex_match(address.toString(), ipv4Range))
        {
            unsigned octets[4] = {0};
            if (sscanf(address.toString().c_str(), "%u.%u.%u.%u", &octets[0], &octets[1], &octets[2], &octets[3]) != 4)
            {
                Logger::getInstance().error() << "Invalid IP address format: " << address.toString() << std::endl;
                return "";
            }
            byteAddress.reserve(4);
            for (int i = 0; i < 4; ++i)
            {
                byteAddress.push_back(static_cast<unsigned char>(octets[i]));
            }
            return byteAddress;
        }
        else
        {
            // Check if address is IPv6
            std::string expandedAddress = expandIPv6Address(address.toString());
            std::regex ipv6Range("^([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}$");
            if (std::regex_match(expandedAddress, ipv6Range))
            {
                // IPv6: parse the address
                byteAddress.reserve(16); // IPv6 is 16 bytes long
                std::stringstream ss(expandedAddress);
                std::string group;
                while (std::getline(ss, group, ':'))
                {
                    unsigned short value = static_cast<unsigned short>(std::stoi(group, nullptr, 16)); // Convert from hex
                    byteAddress.push_back(static_cast<unsigned char>(value >> 8));
                    byteAddress.push_back(static_cast<unsigned char>(value & 0xFF));
                }
                return byteAddress;
            }
            else
            {
                return "";
            }
        }
    }

    bool splitSlashMiddle(const std::string& maskAddress, ByteString& address, uint8_t& mask)
    {
        size_t pos = maskAddress.find('/');
        // Check if the slash exists and is not at the start or end.
        if (pos != std::string::npos && pos != 0 && pos != maskAddress.size() - 1)
        {
            address = maskAddress.substr(0, pos);
            std::string maskString = maskAddress.substr(pos + 1);
            if (isDecimal(maskString))
            {
                mask = static_cast<uint8_t>(std::stoi(maskString));
            }
            return true;
        }
        return false;
    }

    void printVector(const std::vector<std::string>& vec) 
    {
        return; // Disabled
        for (const auto& element : vec) {
            std::cout << std::string(element) << std::endl;
        }
    }

    uint8_t byteMaskToNum(const ByteString& mask) 
    {
        ByteString binMask = Functions::byteToBin(mask);
        return static_cast<uint8_t>(std::count(binMask.begin(), binMask.end(), '1'));
    }

    ByteString numMaskToBin(uint8_t mask)
    {
        ByteString stringMask{};
        for (size_t i = 0; i < mask; i++)
        {
            stringMask += "1";
        }
        for (size_t i = 0; i < (((mask >= 32) ? 128 : 32) - mask); i++)
        {
            stringMask = stringMask + "0";
        }
        return stringMask;
    }

    ByteString computeNetworkAddress(const ByteString& ipAddress, uint8_t mask)
    {   
        if (ipAddress.empty()) return ByteString("");
        size_t bitSize = 0;
                if (ipAddress.size() == 4)
        {
            bitSize = 32;
        }
        else if (ipAddress.size() == 16)
        {
            bitSize = 128;
        }
        else return ByteString("");

        ByteString binMask = numMaskToBin(mask);
        ByteString binIp = byteToBin(ipAddress);

        for (size_t i = 0; i < bitSize; i++)
        {
            if (binMask[i] == '0')
            {
                binIp[i] = '0';
            }
        }
        return binToByte(binIp);
    }

    ByteString byteAddressToNumAddress(const ByteString& ip) {
        std::ostringstream address{};
        for (size_t i = 0; i < ip.size(); ++i) {
            if (i != 0) address << ".";
            address << static_cast<uint8_t>(ip[i]);
        }
        return address.str();
    }

    bool compareNetworkWithIp(ByteString networkAddress, ByteString ipAddress, uint8_t mask)
    {
        if (networkAddress.size() != ipAddress.size()) return false;
        size_t totalBits = networkAddress.size() * 8;
        if (mask > totalBits) return false;

        size_t byteCount = mask / 8;
        size_t remainingBits = mask % 8;

        // ✅ Compare full bytes
        for (size_t i = 0; i < byteCount; ++i)
        {
            if (static_cast<uint8_t>(networkAddress[i]) != static_cast<uint8_t>(ipAddress[i]))
            {
                return false;
            }
        }

        // ✅ Compare remaining bits safely
        if (remainingBits > 0 && byteCount < networkAddress.size())
        {
            uint8_t bitMask = 0xFF << (8 - remainingBits);
            if ((static_cast<uint8_t>(networkAddress[byteCount]) & bitMask) !=
                (static_cast<uint8_t>(ipAddress[byteCount]) & bitMask))
            {
                return false;
            }
        }

        return true;
    }

    ByteString compactNetworkAddress(ByteString network, uint8_t mask)
    {
        ByteString binMask = numMaskToBin(mask);
        ByteString compactNet;
        compactNet.reserve(network.size());

        for (size_t i = 0; i < network.size(); ++i)
        {
            if (binMask[i * 8] == '1')
            {
                compactNet.push_back(network[i]);
            }
            else
            {
                break;
            }
        }
        return compactNet;
    }

    std::optional<ByteString> calculateEui64(ByteString mac, ByteString fullIPv6)
    {
        Logger::getInstance().debug() << "Calculating EUI-64 address with IPv6 address: " << byteToHex(fullIPv6) << " and MAC: " << mac << "." << std::endl;
        // Check if parameters are valid
        if (fullIPv6.size() != 16 || mac.size() != 6) { 
            Logger::getInstance().error() << "EUI-64 invalid parameters.";
            // Return due to invalid parameters
            return nullptr;
        }
        // New variables
        ByteString newAddress;
        ByteString network = fullIPv6.substr(0, 8);
        ByteString host = fullIPv6.substr(8);
        // Checks if host address if blank
        if (host == std::string("\x00\x00\x00\x00\x00\x00\x00\x00", 8))
        {
            ByteString newHost = mac.substr(0, 3) +
                ByteString("\xff\xfe", 2) +
                mac.substr(3, 3);

            // Convert first byte to a binary string
            ByteString firstByte = byteToBin(mac.substr(0, 1));

            // Logging
            Logger::getInstance().debug() << "First byte converted to binary: " << firstByte << "." << std::endl;
            Logger::getInstance().info() << "Flipping seventh bit of EUI-64." << std::endl;

            // Flip 7th (6th index) bit
            if (firstByte[6] == '1') { firstByte[6] = '0'; }
            else if (firstByte[6] == '0') { firstByte[6] = '1'; }

            // Logging
            Logger::getInstance().debug() << "New first byte: " << firstByte << "." << std::endl;

            // Converts back to byte form
            firstByte = binToByte(firstByte, 1);

            Logger::getInstance().info() << "Adding flipped byte back to the host address" << std::endl;

            // Add first byte back to the host portion
            newHost[0] = firstByte[0];

            Logger::getInstance().debug() << "New host portion: " << byteToHex(newHost) << "." << std::endl;

            // Calculate new IPv6 Address
            newAddress = network + newHost;

            return newAddress;
        }
        else
        {
            Logger::getInstance().error() << "Host portion not empty" << std::endl;
            // Host field not empty
            return nullptr;
        }
    }

    ByteString ipv6ToByte(std::string ip)
    {
        unsigned int octets[8] = {0};
        if (sscanf(ip.c_str(), "%x:%x:%x:%x:%x:%x:%x:%x", 
                   &octets[0], &octets[1], &octets[2], &octets[3],
                   &octets[4], &octets[5], &octets[6], &octets[7]) != 8) {
            Logger::getInstance().error() << "Invalid IPv6 address format: " << ip << std::endl;
            return "";
        }
        ByteString byteIP;
        byteIP.reserve(16);
        for (int i = 0; i < 8; ++i) {
            byteIP.push_back(static_cast<unsigned char>((octets[i] >> 8) & 0xFF));
            byteIP.push_back(static_cast<unsigned char>(octets[i] & 0xFF));
        }
        return byteIP;
    }

    bool compareNetworkWithMask(const ByteString& network, uint8_t mask)
    {
        const ByteString binAddress = byteToBin(network);
        const ByteString binMask = numMaskToBin(mask);
        for (size_t i = 0; i < binAddress.size(); ++i)
        {
            if (binMask[i] == '0' && binAddress[i] != '0')
            {
                return false;
            }
        }
        return true;
    }

    ByteString findClassfullNetwork(const ByteString& ip)
    {
        uint32_t ipInt = byteToNum(ip);
        uint32_t network;
        if ((ipInt & 0x80000000) == 0)
        {
            network = ipInt & 0xFF000000; // Class A
        }
        else if ((ipInt & 0xC0000000) == 0x80000000)
        {
            network = ipInt & 0xFFFF0000; // Class B
        }
        else
        {
            network = ipInt & 0xFFFFFF00; // Class C
        }
        return numToByte(network, 4);
    }

    uint8_t getDefaultMask(const ByteString& network)
    {
        uint32_t netInt = byteToNum(network);
        if ((netInt & 0x80000000) == 0) // Class A
        {
            return 8;
        }
        else if ((netInt & 0xC0000000) == 0x80000000) // Class B
        {
            return 16;
        }
        else // Class C
        {
            return 24;
        }
    }

    bool validateMacAddress(const ByteString& mac, const ByteString currentMac)
    {
        if (mac.size() != 6 || currentMac.size() != 6) return false;
        if (mac == currentMac) return true;
        return (static_cast<unsigned char>(mac[0]) & 0x01) != 0;
    }

    bool isSubnetOf(const ByteString &network, uint8_t mask, const ByteString &summaryNetwork, uint8_t summaryMask)
    {
        if (summaryMask > mask) return false;
        ByteString binNetwork = byteToBin(network);
        ByteString binSummary = byteToBin(summaryNetwork);
        for (uint8_t i = 0; i < summaryMask; ++i)
        {
            if (binNetwork[i] != binSummary[i]) return false;
        }
        return true;
    }

    bool isMulticast(const ByteString& ip)
    {
        if (ip.empty()) return false;
        unsigned char firstByte = static_cast<unsigned char>(ip[0]);
        if (firstByte >= 224 && firstByte <= 239) return true;
        if (firstByte == 0xFF) return true;
        return false;
    }

    std::string expandIPv6Address(const std::string& ipv6Address) 
    {
        if (!isIPv6Address(ipv6Address) && !isIPv6AddressWithMask(ipv6Address))
        {
            return "";
        }

        // Seperate any prefix (e.g., /64) from the IP address
        std::string ip, prefix;
        {
            size_t slashPos = ipv6Address.find('/');
            if (slashPos != std::string::npos)
            {
                ip = ipv6Address.substr(0, slashPos);
                prefix = ipv6Address.substr(slashPos); // Keeps the '/' + prefix
            }
            else
            {
                ip = ipv6Address;
            }
        }

        // Check for '::' (indicates compressed zero block)
        size_t doubleColonPos = ip.find("::");
        std::string expandedIP;

        if (doubleColonPos != std::string::npos)
        {
            // Split into front/back around the "::"
            std::vector<std::string> frontSegments = tokenize(ip.substr(0, doubleColonPos), ':');
            std::vector<std::string> backSegments = tokenize(ip.substr(doubleColonPos + 2), ':');

            // Calculate how many hextets are missing
            // (IPv6 has exactly 8 hextets)
            size_t hextetCount = frontSegments.size() + backSegments.size();
            size_t missingCount = 8 - hextetCount;

            // Combine into one vector
            std::vector<std::string> allSegments;
            allSegments.reserve(8);

            // 1. front segments
            for (auto& seg : frontSegments)
            {
                allSegments.push_back(seg);
            }
            // 2. insert the missing zero segments
            for (size_t i = 0; i < missingCount; ++i)
            {
                allSegments.push_back("0");
            }
            // 3. back segments
            for (auto& seg : backSegments)
            {
                allSegments.push_back(seg);
            }

            // Now pad each segment to 4 hex digites and join them with ':'
            for (size_t i = 0; i < allSegments.size(); ++i)
            {
                expandedIP += padWithZeros(allSegments[i]);
                if (i < allSegments.size() - 1)
                {
                    expandedIP += ":";
                }
            }
        }
        else
        {
            std::vector<std::string> segments = tokenize(ip, ':');
            for (size_t i = 0; i < segments.size(); ++i)
            {
                expandedIP += padWithZeros(segments[i]);
                if (i < segments.size() - 1)
                {
                    expandedIP += ":";
                }
            }
        }

        // Append the prefix (e.g., /64) if it exists
        expandedIP += prefix;
        return expandedIP;
    }

    bool isIPv6Address(const std::string& address) 
    {
        std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))");
        return std::regex_match(address, ipRegex);
    }

    bool isIPv6AddressWithMask(const std::string& addressWithMask) 
    {
        std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))/(12[0-8]|1[01][0-9]|[1-9]?[0-9])");
        return std::regex_match(addressWithMask, ipRegex);
    }

    bool isMACAddress(const std::string& macAddress) 
    {
        std::regex macRegex("^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$");
        return std::regex_match(macAddress, macRegex);
    }

    std::vector<std::string> tokenize(const std::string& input, char delimiter) 
    {
        // Vector to store the resulting tokens
        std::vector<std::string> tokens;

        // Temporary string to store each token during iteration
        std::string token;

        // Use an input string stream for easy parsing
        std::istringstream tokenStream(input);

        // Split the string based on the delimiter
        while (std::getline(tokenStream, token, delimiter)) 
        {
            tokens.push_back(token);
        }

        return tokens;
    }

    std::string padWithZeros(const std::string& input) 
    {
        std::ostringstream paddedStream;
        paddedStream << std::setfill('0') << std::setw(4) << input;
        return paddedStream.str();
    }

    bool isLocalLink(const ByteString& input)
    {
        return (byteToBin(input.substr(0, 2)).substr(0, 10) == "1111111010");
    }
    
    #pragma endregion
    #pragma region Other

    size_t getRandomBetween(size_t min, size_t max) {
        if (min > max) std::swap(min, max);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(static_cast<int>(min), static_cast<int>(max));
        return static_cast<size_t>(dis(gen));
    }

    ByteString changeSize(ByteString str, size_t size, ByteString value) 
    {
        if (str.size() >= size) return str;
        str.reserve(size);
        while (str.size() < size)
        {
            str = value + str;
        }
        return str;
    }

    ByteString reverseBinary(const ByteString& binary) 
    {
        ByteString reversed = binary; // Create a copy of the input string

        for (size_t i = 0; i < binary.size(); i++) {
            if (binary[i] == '0') {
                reversed[i] = '1'; // Change '0' to '1'
            } else if (binary[i] == '1') {
                reversed[i] = '0'; // Change '1' to '0'
            }
        }

        return reversed; // Return the modified string
    }

    std::string timeToString(const std::chrono::system_clock::time_point time)
    {
        std::time_t time_t_value = std::chrono::system_clock::to_time_t(time);
        return std::to_string(time_t_value);
    }

    #pragma endregion
}
