#include <Functions.h>

Functions* Functions::singleton = nullptr;

Functions::Functions() {}

Functions* Functions::getInstance() {
    if (singleton == nullptr) {
        singleton = new Functions();
    }
    return singleton;
}

bool Functions::isBinary(const std::string& str) {
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

std::string Functions::binToHex(const std::string& binaryStr) {
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

bool Functions::isHex(const std::string& str) {
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

std::string Functions::hexDigitToBin(char hexDigit) {
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

std::string Functions::hexToBin(const std::string& hexStr) {
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

bool Functions::stringToBool(const std::string& str) {
    if (str.empty()) {return false;}
    if (str == "1") {
        return true;
    }
    if (str == "0") {
        return false;
    }
    return false;
}

std::string Functions::binToByte(const std::string& binaryData) {
    return hexToByte(binToHex(binaryData));
}

std::string Functions::hexToByte(const std::string& hexBinaryData) {
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
    
    return byteString;
}

std::string Functions::byteToHex(const std::string& input) {
    std::stringstream ss;
    for (unsigned char byte : input) {
        ss << charToHex(byte);
    }
    return ss.str();
}

std::string Functions::charToHex(unsigned char byte) {
    std::stringstream ss;
    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    return ss.str();
}

std::string Functions::charToBin(unsigned char byte) {
    std::bitset<8> bits(byte);
    return bits.to_string();
}

std::string Functions::byteToBin(const std::string& input) {
    if (input.empty()) {return "";}
    std::stringstream ss;
    for (size_t i = 0; i < input.length(); ++i) {
        ss << charToBin(static_cast<unsigned char>(input[i]));
    }
    return ss.str();
}

uint8_t Functions::binToDec(const std::string& binary) {
    int decimal = 0;
    int length = binary.length();
    for (int i = 0; i < length; ++i) {
        if (binary[length - 1 - i] == '1') {
            decimal += pow(2, i);
        }
    }
    return decimal;
}

void Functions::printVector(const std::vector<std::string>& vec) {
    for (const auto& element : vec) {
        
        //std::cout << std::string(element) << std::endl;
    }
}

std::string Functions::lowerCase(std::string str) {
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

int Functions::stringToNum(std::string num) {
    int number;
#ifdef _WIN32
    sscanf_s(num.c_str(), "%d", &number);
#else
    sscanf(num.c_str(), "%d", &number);
#endif
    return number;
}

std::string Functions::byteArrayToHex(const std::vector<uint8_t>& byte_array) {
    if (byte_array.empty()) {return "";}
    std::stringstream hex_stream;
    for (auto byte : byte_array) {
        hex_stream << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(byte);
    }
    return hex_stream.str();
}

unsigned Functions::hexToNum(const std::string& hexStr) {
    unsigned decimalValue;
    std::stringstream ss;

    ss << std::hex << hexStr;
    ss >> decimalValue;

    return decimalValue;
}

std::string Functions::intToHex(int num) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << num;ss.str();
    std::string hex = ss.str();
    if (hex.size() % 2 != 0) {
        hex = "0" + hex;
    }

    return hex;
}

std::string Functions::intToHexWithByte(uint32_t num, int byteSize) {
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



std::string Functions::addressToHex(const std::string& mask) {
int oct1, oct2, oct3, oct4;
#ifdef _WIN32
	sscanf_s(mask.c_str(), "%d.%d.%d.%d", &oct1, &oct2, &oct3, &oct4);
#else
	sscanf(mask.c_str(), "%d.%d.%d.%d", &oct1, &oct2, &oct3, &oct4);
#endif
    std::string newmask = intToHex(oct1) + intToHex(oct2) + intToHex(oct3) + intToHex(oct4);
    return newmask;
}

int Functions::hexMaskToInt(const std::string& mask) 
{
    int maskInt{0};
    std::string maskInBin = hexToBin(mask);
    for (const char bin : maskInBin)
    {
        if (bin == '1')
        {
            maskInt++;
        }
    }
    return maskInt;
}

std::string Functions::computeNetworkAddress(const std::string& ipAddress, int mask)
{   
    std::string binMask = intMaskToBin(mask);
    std::string binIp = hexToBin(ipAddress);

    for (int i = 0; i < 32; i++)
    {
        if (binMask[i] == '0')
        {
            binIp[i] = '0';
        }
    }
    return binToHex(binIp);
}

std::string Functions::intMaskToBin(int mask)
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

std::string Functions::getIPAddress(const std::string& ip) {
    std::string address{};
    for (size_t i = 0; i < ip.length(); ++i) {
        address += "." + std::to_string(hexToNum(charToHex(ip[i])));
    }
    return address.substr(1);
}

std::string Functions::boolToString(bool bol) {
    if (bol) {
        return "1";
    } else {
        return "0";
    }
}

int Functions::getRandomBetween(int min, int max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}

int Functions::byteToNum(std::string str) {
    return hexToNum(byteToHex(str));
}

std::string Functions::intToByte(int num) {
    return hexToByte(intToHex(num));
}

std::string Functions::changeSize(std::string str, int size, std::string value) {
    while (str.size() < size) {
        str = value + str;
    }
    return str;
}

bool Functions::compareNetworkWithIp(std::string networkAddress, std::string ipAddress)
{
    bool compare = false;
    std::transform(ipAddress.begin(), ipAddress.end(), ipAddress.begin(), ::toupper);
    for (int i = 0; i < ipAddress.size(); i += 2)
    {
        std::string oct = networkAddress.substr(i, 2);
        if (oct == "00" || oct == ipAddress.substr(i, 2))
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

std::string Functions::reverseBinary(const std::string& binary) {
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

std::string Functions::compactNetworkAddress(std::string network, int mask)
{
    std::string networkAddress = network;
    std::string binMask = binToByte(intMaskToBin(mask));
    for (int i = 3; i >= 0; i--)
    {
        if (binMask[i] == 0x00)
        networkAddress.erase(i);
    }
    return networkAddress;
}