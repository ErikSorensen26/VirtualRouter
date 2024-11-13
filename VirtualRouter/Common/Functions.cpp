
#include "Functions.h"

namespace Functions {

    #pragma region TestIf

    bool isBinary(const std::string& str) {
        if (str.empty()) {
            return false;
        }
        for (char c : str) {
            if (c != '0' && c != '1') {
                return false;
            }
        }
        return true;
    }

    bool isHex(const std::string& str) {
        if (str.empty()) {
            return false;
        }
        for (char c : str) {
            if (!std::isxdigit(c)) {
                return false;
            }
        }
        return true;
    }

    #pragma endregion
    #pragma region ByteConv

    std::string byteToHex(const std::string& input) {
        std::stringstream ss;
        for (unsigned char byte : input) {
            ss << charToHex(byte);
        }
        return ss.str();
    }

    std::string byteToBin(const std::string& input) {
        if (input.empty()) {return "";}
        std::stringstream ss;
        for (size_t i = 0; i < input.length(); ++i) {
            ss << charToBin(static_cast<unsigned char>(input[i]));
        }
        return ss.str();
    }

    std::string byteArrayToHex(const std::vector<uint8_t>& byte_array) {
        if (byte_array.empty()) {return "";}
        std::stringstream hex_stream;
        for (auto byte : byte_array) {
            hex_stream << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(byte);
        }
        return hex_stream.str();
    }

    int byteToNum(std::string str) {
        return hexToNum(byteToHex(str));
    }

    #pragma endregion
    #pragma region HexConv

    std::string binToHex(const std::string& binaryStr) {
        if (binaryStr.empty()) {return "";}
        std::stringstream ss;
        std::string paddedBinaryStr = binaryStr;
        int padLength = 4 - (binaryStr.length() % 4);
        if (padLength != 4) {
            paddedBinaryStr = std::string(padLength, '0') + binaryStr;
        }
        for (size_t i = 0; i < paddedBinaryStr.length(); i += 4) {
            std::string fourBits = paddedBinaryStr.substr(i, 4);
            ss << std::hex << std::uppercase << std::bitset<4>(fourBits).to_ulong();
        }
        return ss.str();
    }

    std::string hexDigitToBin(char hexDigit) {
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

    std::string hexToBin(const std::string& hexStr) {
        if (hexStr.empty()) {return "";}
        std::string binaryStr;
        for (char hexDigit : hexStr) {
            std::string binStr = hexDigitToBin(hexDigit);
            if (binStr.empty()) {
                std::cerr << "Invalid hexadecimal digit: " << hexDigit << std::endl;
                return "";
            }
            binaryStr += binStr;
        }
        return binaryStr;
    }

    std::string hexToByte(const std::string& hexBinaryData, size_t size) {
        if (hexBinaryData.empty()) {
            return "";
        }

        std::string byteString;
        int length = hexBinaryData.size();

        if (length % 2 != 0) {
            byteString = "0" + byteString;
            length = hexBinaryData.length();
        }

        byteString.reserve(length / 2);  // Reserve space to avoid multiple allocations

        for (int i = 0; i < length; i += 2) {
            std::string byteStr = hexBinaryData.substr(i, 2);
            unsigned char byte = static_cast<unsigned char>(std::stoi(byteStr, nullptr, 16));
            byteString.push_back(byte);
        }

        if (size != 0)
        {
            byteString = changeSize(byteString, size, std::string("\x00", 1));
        }

        return byteString;
    }

    unsigned hexToNum(const std::string& hexStr) {
        unsigned decimalValue;
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

    std::string binToByte(const std::string& binaryData, size_t size) {
        return hexToByte(binToHex(binaryData), size);
    }

    uint8_t binToNum(const std::string& binary) {
        int decimal = 0;
        int length = binary.length();
        for (int i = 0; i < length; ++i) {
            if (binary[length - 1 - i] == '1') {
                decimal += pow(2, i);
            }
        }
        return decimal;
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
    			normalize += tolower(c);
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

    int stringToNum(std::string num) {
        int number;
    #ifdef _WIN32
        sscanf_s(num.c_str(), "%d", &number);
    #else
        sscanf(num.c_str(), "%d", &number);
    #endif
        return number;
    }

    #pragma endregion
    #pragma region CharConv

    std::string charToHex(unsigned char byte) {
        std::stringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        return ss.str();
    }

    std::string charToBin(unsigned char byte) {
        std::bitset<8> bits(byte);
        return bits.to_string();
    }

    #pragma endregion
    #pragma region NumChar

    std::string numToHex(int num) {
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << num;ss.str();
        std::string hex = ss.str();
        if (hex.size() % 2 != 0) {
            hex = "0" + hex;
        }

        return hex;
    }

    std::string numToHexWithByte(uint32_t num, int byteSize) {
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
        std::string hex = ss.str();

        return hex;
    }

    std::string numToByte(int num, size_t size) {
        return hexToByte(numToHex(num), size);
    }

    #pragma endregion
    #pragma region NetConv

    std::string addressToByte(const std::string& mask) {
    int oct1, oct2, oct3, oct4;
    #ifdef _WIN32
    	sscanf_s(mask.c_str(), "%d.%d.%d.%d", &oct1, &oct2, &oct3, &oct4);
    #else
    	sscanf(mask.c_str(), "%d.%d.%d.%d", &oct1, &oct2, &oct3, &oct4);
    #endif
        std::string newmask = numToHex(oct1) + numToHex(oct2) + numToHex(oct3) + numToHex(oct4);
        return hexToByte(newmask);
    }

    void printVector(const std::vector<std::string>& vec) {
        for (const auto& element : vec) {

            //std::cout << std::string(element) << std::endl;
        }
    }

    int byteMaskToNum(const std::string& mask) 
    {
        int maskInt{0};
        std::string maskInBin = byteToBin(mask);
        for (const char bin : maskInBin)
        {
            if (bin == '1')
            {
                maskInt++;
            }
        }
        return maskInt;
    }

    std::string numMaskToBin(int mask)
    {
        std::string stringMask{};
        for (int i = 0; i < mask; i++)
        {
            stringMask += "1";
        }
        for (int i = 0; i < (32 - mask); i++)
        {
            stringMask = stringMask + "0";
        }
        return stringMask;
    }

    std::string computeNetworkAddress(const std::string& ipAddress, int mask)
    {   
        std::string binMask = numMaskToBin(mask);
        std::string binIp = byteToBin(ipAddress);

        for (int i = 0; i < 32; i++)
        {
            if (binMask[i] == '0')
            {
                binIp[i] = '0';
            }
        }
        return binToByte(binIp);
    }

    std::string byteAddressToNumAddress(const std::string& ip) {
        std::ostringstream address{};
        for (size_t i = 0; i < ip.length(); ++i) {
            if (i != 0) address << ".";
            address << static_cast<uint8_t>(ip[i]);
        }
        return address.str();
    }

    bool compareNetworkWithIp(std::string networkAddress, std::string ipAddress)
    {
        bool compare = false;
        std::transform(ipAddress.begin(), ipAddress.end(), ipAddress.begin(), ::toupper);
        for (int i = 0; i < ipAddress.size(); i += 1)
        {
            std::string oct = networkAddress.substr(i, 1);
            if (oct == std::string("\x00", 1) || oct == ipAddress.substr(i, 1))
            {
                compare = true;
            }
            else
            {
                return false;
            }
        }
        return compare;
    }

    std::string compactNetworkAddress(std::string network, int mask)
    {
        std::string networkAddress = network;
        std::string binMask = binToByte(numMaskToBin(mask));
        for (int i = 3; i >= 0; i--)
        {
            if (binMask[i] == 0x00)
            networkAddress.erase(i);
        }
        return networkAddress;
    }

    #pragma endregion
    #pragma region Other

    int getRandomBetween(int min, int max) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(min, max);
        return dis(gen);
    }

    std::string changeSize(std::string str, size_t size, std::string value) {
        while (str.size() < size) {
            str = value + str;
        }
        return str;
    }

    std::string reverseBinary(const std::string& binary) {
        std::string reversed = binary; // Create a copy of the input string

        for (size_t i = 0; i < binary.size(); i++) {
            if (binary[i] == '0') {
                reversed[i] = '1'; // Change '0' to '1'
            } else if (binary[i] == '1') {
                reversed[i] = '0'; // Change '1' to '0'
            }
        }

        return reversed; // Return the modified string
    }
}