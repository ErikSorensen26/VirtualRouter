#include "Functions.h"
#include <Logger.h>
#include <bitset>
#include <random>

namespace Functions {

    #pragma region TestIf

    bool isBinary(const ByteString& str) {
        if (str.empty()) {
            return false;
        }
        for (auto c : str) {
            if (c != '0' && c != '1') {
                return false;
            }
        }
        return true;
    }

    bool isHex(const ByteString& str) {
        if (str.empty()) {
            return false;
        }
        for (auto c : str) {
            if (!std::isxdigit(c)) {
                return false;
            }
        }
        return true;
    }

    bool isDecimal(const std::string& str)
    {
        return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
    }

    #pragma endregion
    #pragma region ByteConv

    ByteString byteToHex(const ByteString& input) {
        std::stringstream ss;
        for (auto& byte : input) {
            ss << charToHex(static_cast<uint8_t>(byte));
        }
        return ss.str();
    }

    ByteString byteToBin(const ByteString& input) {
        if (input.empty()) {return "";}
        std::stringstream ss;
        for (size_t i = 0; i < input.size(); ++i) {
            ss << charToBin(static_cast<unsigned char>(input[i]));
        }
        return ss.str();
    }

    ByteString byteArrayToHex(const std::vector<uint8_t>& byte_array) {
        if (byte_array.empty()) {return "";}
        std::stringstream hex_stream;
        for (auto byte : byte_array) {
            hex_stream << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(byte);
        }
        return hex_stream.str();
    }

    uint32_t byteToNum(ByteString str) {
        return hexToNum(byteToHex(str));
    }

    #pragma endregion
    #pragma region HexConv

    ByteString binToHex(const ByteString& binaryStr) {
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

    ByteString hexDigitToBin(uint8_t hexDigit) {
        switch (hexDigit) {
            case '0': return "0000";
            case '1': return "0001";
            case '2': return "0010";
            case '3': return "0011";
            case '4': return "0100";
            case '5': return "0101";
            case '6': return "0110";
            case '7': return "0111";
            case '8': return "1000";
            case '9': return "1001";
            case 'A': case 'a': return "1010";
            case 'B': case 'b': return "1011";
            case 'C': case 'c': return "1100";
            case 'D': case 'd': return "1101";
            case 'E': case 'e': return "1110";
            case 'F': case 'f': return "1111";
            default: return "";
        }
    }

    ByteString hexToBin(const ByteString& hexStr) {
        if (hexStr.empty()) {return "";}
        ByteString binaryStr;
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
        if (hexBinaryData.empty()) {
            return "";
        }

        ByteString byteString;
        size_t length = hexBinaryData.size();

        if (length % 2 != 0) {
            byteString = ByteString("0") + byteString;
            length = hexBinaryData.size();
        }

        byteString.reserve(length / 2);  // Reserve space to avoid multiple allocations

        for (size_t i = 0; i < length; i += 2) {
            ByteString byteStr = hexBinaryData.substr(i, 2);
            unsigned char byte = static_cast<unsigned char>(std::stoi(byteStr.toString(), nullptr, 16));
            byteString.push_back(byte);
        }

        if (size != 0)
        {
            byteString = changeSize(byteString, size, ByteString("\x00", 1));
        }

        return byteString;
    }

    uint32_t hexToNum(const ByteString& hexStr) {
        uint32_t decimalValue;
        std::stringstream ss;

        ss << std::hex << hexStr;
        ss >> decimalValue;

        return decimalValue;
    }

    #pragma endregion
    #pragma region BoolConv

    std::string boolToString(bool bol) {
        if (bol) {
            return "1";
        } else {
            return "0";
        }
    }

    #pragma endregion
    #pragma region BinConv

    ByteString binToByte(const ByteString& binaryData, size_t size) {
        return hexToByte(binToHex(binaryData), size);
    }

    uint32_t binToNum(const ByteString& binary) {
        double decimal = 0;
        size_t length = binary.size();
        for (size_t i = 0; i < length; ++i) {
            if (binary[length - 1 - i] == '1') {
                decimal += pow(2, static_cast<double>(i));
            }
        }
        return static_cast<unsigned int>(decimal);
    }

    #pragma endregion
    #pragma region StringConv

    std::string lowerCase(std::string str) {
        if (str.empty()) {return "";}
        std::string normalize;
        for (char c : str) {
            if (isspace(c) || c == '\t') {
                normalize += c;
            }
            else {
                normalize += static_cast<char>(tolower(c));
            }
    	}
        return normalize;
    }

    bool stringToBool(const std::string& str) {
        if (str.empty()) {return false;}
        if (str == "1") {
            return true;
        }
        if (str == "0") {
            return false;
        }
        return false;
    }

    uint32_t stringToNum(std::string num) {
        unsigned int number;
        sscanf(num.c_str(), "%d", &number);
        return number;
    }

    #pragma endregion
    #pragma region CharConv

    ByteString charToHex(uint8_t byte) {
        std::stringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        return ss.str();
    }

    ByteString charToBin(uint8_t byte) {
        std::bitset<8> bits(byte);
        return bits.to_string();
    }

    #pragma endregion
    #pragma region NumChar

    ByteString numToHex(uint32_t num, size_t size) {
        size_t newSize;
        if (size != 0)
        {
            newSize = size;
        }
        else
        {
            newSize = 2;
        }
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setw(static_cast<int>(newSize)) << std::setfill('0') << num;ss.str();
        ByteString hex = ss.str();
        if (size == 0)
        {
            if (hex.size() % 2 != 0) {
                hex = ByteString("0") + hex;
            }
        }

        return hex;
    }

    ByteString numToHexWithByte(uint32_t num, int byteSize) {
        if (byteSize <= 0) {
            throw std::invalid_argument("byteSize must be positive");
        }

        // Calculate the maximum value based on byteSize
        uint32_t maxVal = 0;
        for (int i = 0; i < byteSize; ++i) {
            maxVal = (maxVal << 8) | 0xFF;
        }

        if (num > maxVal) {
            throw std::out_of_range("Number exceeds the maximum value for the specified byte size");
        }

        // Convert number to network byte order
        uint32_t networkOrderNum;
        if (byteSize == 1) {
            networkOrderNum = num & 0xFF;
        } else if (byteSize == 2) {
            networkOrderNum = htons(static_cast<uint16_t>(num));
        } else if (byteSize == 4) {
            networkOrderNum = htonl(static_cast<uint32_t>(num));
        } else {
            throw std::invalid_argument("Unsupported byte size. Supported sizes: 1, 2, 4");
        }

        // Convert to hex string
        std::ostringstream ss;
        ss << std::hex << std::uppercase << std::setw(byteSize * 2) << std::setfill('0') << networkOrderNum;
        ByteString hex = ss.str();

        return hex;
    }

    ByteString numToByte(uint32_t num, size_t size) {
        return hexToByte(numToHex(num), size);
    }

    #pragma endregion
    #pragma region NetConv

    ByteString addressToByte(const ByteString& mask) {
        uint32_t oct1, oct2, oct3, oct4;
    	sscanf(mask.toString().c_str(), "%d.%d.%d.%d", &oct1, &oct2, &oct3, &oct4);
        ByteString newMask = numToHex(oct1) + numToHex(oct2) + numToHex(oct3) + numToHex(oct4);
        return hexToByte(newMask);
    }

    void printVector(const std::vector<std::string>& vec) {
        return; // Disabled
        for (const auto& element : vec) {
            std::cout << std::string(element) << std::endl;
        }
    }

    uint8_t byteMaskToNum(const ByteString& mask) 
    {
        uint8_t maskInt{0};
        ByteString maskInBin = byteToBin(mask);
        for (const auto bin : maskInBin)
        {
            if (bin == '1')
            {
                maskInt++;
            }
        }
        return maskInt;
    }

    ByteString numMaskToBin(uint8_t mask)
    {
        ByteString stringMask{};
        for (size_t i = 0; i < mask; i++)
        {
            stringMask += "1";
        }
        for (size_t i = 0; i < (32 - mask); i++)
        {
            stringMask = stringMask + "0";
        }
        return stringMask;
    }

    ByteString computeNetworkAddress(const ByteString& ipAddress, uint8_t mask)
    {   
        ByteString binMask = numMaskToBin(mask);
        ByteString binIp = byteToBin(ipAddress);

        for (size_t i = 0; i < 32; i++)
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
        // Ensure addresses are the same length
        if (networkAddress.size() != ipAddress.size())
        {
            return false;
        }

        size_t totalBits = networkAddress.size() * 8;

        // Validate previx length
        if (mask > totalBits)
        {
            return false;
        }

        // Calculate byte length and reminder bit for the mask
        size_t fullBytes = mask / 8;
        size_t remainingBits = mask % 8;
        
        // Compare each byte
        for (size_t i = 0; i < networkAddress.size(); ++i)
        {
            uint8_t subnetMask = 0xFF; // Default mask for full byte
            if (i == fullBytes)
            {
                // Create a partial mask for the last byte
                mask = static_cast<uint8_t>(0xFF << (8 - remainingBits));
            }
            else if (i > fullBytes)
            {
                mask = 0x00; // Beyone the prefix, mask is all zeros
            }

            // Apply the mask and compare the bytes
            if ((networkAddress[i] & subnetMask) != (ipAddress[i] & subnetMask))
            {
                return false;
            }
        }

        return true; // All bytes match
    }

    ByteString compactNetworkAddress(ByteString network, uint8_t mask)
    {
        ByteString networkAddress = network;
        ByteString binMask = binToByte(numMaskToBin(mask));
        for (size_t i = 3; i >= 0; i--)
        {
            if (binMask[i] == 0x00)
            networkAddress.erase(i, 1);
        }
        return networkAddress;
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
        unsigned int oct1, oct2, oct3, oct4, oct5, oct6, oct7, oct8;
    	sscanf(ip.c_str(), "%d:%d:%d:%d:%d:%d:%d:%d", &oct1, &oct2, &oct3, &oct4, &oct5, &oct6, &oct7, &oct8);
        ByteString newmask = numToByte(oct1, 2) + numToByte(oct2, 2) + numToByte(oct3, 2) + numToByte(oct4, 2) + numToByte(oct5, 2) + numToByte(oct6, 2) + numToByte(oct7, 2) + numToByte(oct8, 2);
        return newmask;
    }

    bool compareNetworkWithMask(const ByteString& network, uint8_t mask)
    {
        const ByteString binAddress = byteToBin(network);
        const ByteString binMask = numMaskToBin(mask);
        for (size_t i = 0; i < 32; ++i)
        {
            if (binMask[i] == '0')
            {
                if (binAddress[i] != 0)
                {
                    return false;
                }
            }
        }
        return true;
    }

    ByteString findClassfullNetwork(ByteString& ip)
    {
        uint32_t ipInt = byteToNum(ip);
        uint32_t network = ipInt & 0xFF000000; // Class A
        if (network >= 0xC0000000)
        {
            network = ipInt & 0xFFFFFF00; // Class C
        }
        else if (network >= 0x80000000)
        {
            network = ipInt & 0xFFFF0000; // Class B
        }
        return numToByte(network, 4);
    }

    uint8_t getDefaultMask(const ByteString& network)
    {
        uint32_t netInt = static_cast<uint32_t>(byteToNum(network));
        if (netInt <= 0x7FFFFFFF) // Class A
        {
            return 8;
        }
        else if (netInt <= 0xBFFFFFFF) // Class B
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
        // Ensure the byte string is a valid length
        if (mac.size() != 6 || currentMac.size() != 6)
        {
            return false;
        }

        // Check if the mac matches the current MAC
        if (mac == currentMac)
        {
            return true;
        }

        // Check for multicast/broadcast by examining the LSB of the first byte
        if (static_cast<unsigned char>(mac[0]) & 0x01)
        {
            return true;
        }
        
        return false;
    }

    bool isSubnetOf(const ByteString &network, uint8_t mask, const ByteString &summaryNetwork, uint8_t summaryMask)
    {
        if (summaryMask > mask)
        {
            // Summary maske cannot be more specific then the network mask
            return false;
        }

        // Calculate binary strings of each network address
        ByteString binNetwork = byteToBin(network);
        ByteString binSummary = byteToBin(summaryNetwork);

        for (size_t i = 31; i >- 0; --i)
        {
            if (binSummary[i] != '0')
            {
                if (binSummary[i] != binNetwork[i])
                {
                    // Not part of the subnet
                    return false;
                }
            }
        }
        // Success
        return true;
    }

    bool isMulticast(const ByteString& ip)
    {
        if (ip.empty()) return false;

        // IPv4 multicast: first byte between 224 and 239
        uint8_t firstByte = static_cast<uint8_t>(ip.toString()[0]);
        if (firstByte >= 224 && firstByte <= 239)
            return true;

        // IPv6 multicast: first byte is 0xFF
        if (ip.size() >= 1 && ip.toString()[0] == '\xFF')
            return true;

        return false;
    }
    
    #pragma endregion
    #pragma region Other

    size_t getRandomBetween(size_t min, size_t max) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(static_cast<int>(min), static_cast<int>(max));
        return static_cast<size_t>(dis(gen));
    }

    ByteString changeSize(ByteString str, size_t size, ByteString value) {
        while (str.size() < size) {
            str = value + str;
        }
        return str;
    }

    ByteString reverseBinary(const ByteString& binary) {
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
}
