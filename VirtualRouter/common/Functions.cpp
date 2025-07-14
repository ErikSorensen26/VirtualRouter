#include "Functions.h"
#include <Logger.h>
#include <bitset>
#include <random>
#include <regex>
#include <cstring>

double secondsSinceEpoch()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string Functions::boolToString(bool bol) 
{
    return bol ? "1" : "0";
}

std::string Functions::lowerCase(std::string str) 
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) -> unsigned char {
        return static_cast<unsigned char>(std::tolower(c));
    });
    return str;
}

uint8_t* Functions::addressToByte(uint8_t* data, const std::string address)
{
    // Check for ipv4
    std::regex ipv4Range("^([0-9]{1,3}\\.){3}[0-9]{1,3}$");
    if (std::regex_match(address, ipv4Range))
    {
        if (sscanf(address.c_str(), "%u.%u.%u.%u", &data[0], &data[1], &data[2], &data[3]) != 4)
        {
            Logger::getInstance().error() << "Invalid IP address format: " << address << std::endl;
            return nullptr;
        }
        return data;
    }
    else
    {
        // Check if address is IPv6
        std::string expandedAddress = expandIPv6Address(address);
        std::regex ipv6Range("^([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}$");
        if (std::regex_match(expandedAddress, ipv6Range))
        {
            // IPv6: parse the address
            std::stringstream ss(expandedAddress);
            std::string group;
            uint8_t count = 0;
            while (std::getline(ss, group, ':'))
            {
                unsigned short value = static_cast<unsigned short>(std::stoi(group, nullptr, 16)); // Convert from hex
                data[count++] = static_cast<unsigned char>(value >> 8);
                data[count++] = static_cast<unsigned char>(value & 0xFF);
            }
            return data;
        }
        else
        {
            return nullptr;
        }
    }
}

uint8_t* Functions::splitSlashMiddle(const std::string& maskAddress, uint8_t* address, uint8_t& mask)
{
    size_t pos = maskAddress.find('/');
    // Check if the slash exists and is not at the start or end.
    if (pos != std::string::npos && pos != 0 && pos != maskAddress.size() - 1)
    {
        std::memcpy(address, maskAddress.data(), pos);
        std::string maskString = maskAddress.substr(pos + 1);
        mask = static_cast<uint8_t>(std::stoi(maskString));
        return address;
    }
    return nullptr;
}

std::optional<std::pair<std::string, std::string>> Functions::splitMiddle(const std::string& full, char delimiter)
{
    size_t pos = full.find(delimiter);
    // Check if the slash exists and is not at the start or end.
    std::string h1;
    std::string h2;
    if (pos != std::string::npos && pos != 0 && pos != full.size() - 1)
    {
        std::pair<std::string, std::string> pair;
        pair.first = full.substr(0, pos);
        pair.second = full.substr(pos + 1);
        return pair;
    }
    return std::nullopt;
}

uint8_t* Functions::computeNetworkAddress(uint8_t* out, const uint8_t* ipAddress, uint8_t prefix, AddressFamily af)
{    
    if (af == AddressFamily::IPv4)
    {
        if (prefix > 32) prefix = 32;
        uint32_t addr;
        std::memcpy(&addr, ipAddress, 4);
        addr = ntohl(addr);
        uint32_t mask = (prefix == 0) ? 0 : (0xFFFFFFFF << (32 - prefix));
        addr &= mask;
        addr = htonl(addr);
        std::memcpy(out, &addr, 4);
    }
    else if (af == AddressFamily::IPv6)
    {
        if (prefix > 128) prefix = 128;
        std::memcpy(out, ipAddress, 16);
        size_t full_bytes = prefix / 8;
        uint8_t remaining = prefix % 8;
        if (remaining != 0 && full_bytes < 16)
        {
            out[full_bytes] &= static_cast<uint8_t>(0xFF << (8 - remaining));
            ++full_bytes;
        }
        for (size_t i = full_bytes; i < 16; ++i)
        {
            out[i] = 0;
        }
    }
    return out;
}

std::string Functions::byteAddressToNumAddress(const uint8_t* ip) {
    std::ostringstream address{};
    for (size_t i = 0; i < 4; ++i) {
        if (i != 0) address << ".";
        address << std::to_string(ip[i]);
    }
    return address.str();
}

bool Functions::compareNetworkWithIp(const uint8_t* networkAddress, const uint8_t* ipAddress, uint8_t mask, AddressFamily af)
{
    size_t maxLen = (af == AddressFamily::IPv4) ? 4 : 16;
    if (mask > maxLen * 8) mask = maxLen * 8;
    size_t fullBytes = mask / 8;
    uint8_t remainingBits = mask % 8;
    for (size_t i = 0; i < fullBytes; ++i)
    {
        if (networkAddress[i] != ipAddress[i])
            return false;
    }
    if (remainingBits != 0)
    {
        uint8_t maskByte = static_cast<uint8_t>(0xFF << (8 - remainingBits));
        if ((networkAddress[fullBytes] & maskByte) != (ipAddress[fullBytes] & maskByte))
            return false;
    }
    return true;
}

size_t Functions::compactNetworkAddress(uint8_t* data, const uint8_t* network, uint8_t prefix, AddressFamily af)
{
    size_t maxLen = (af == AddressFamily::IPv4) ? 4 : 16;
    if (prefix > maxLen * 8) prefix = static_cast<uint8_t>(maxLen * 8);
    uint8_t masked[16] = {};
    std::memcpy(masked, network, maxLen);
    size_t fullBytes = prefix / 8;
    uint32_t remainingBits = prefix % 8;
    if (remainingBits != 0 && fullBytes < maxLen) 
        masked[fullBytes] &= static_cast<uint8_t>(0xFF << (8 - remainingBits));
    for (size_t i = fullBytes + (remainingBits ? 1 : 0); i < maxLen; ++i)
        masked[i] = 0;
    size_t bytesToCopy = (prefix + 7) / 8;
    std::memcpy(data, masked, bytesToCopy);
    return bytesToCopy;
}

uint8_t* Functions::calculateEui64(uint8_t* out, const uint8_t* prefix, const uint8_t* mac)
{
    std::memcpy(out, prefix, 8);
    out[8] = mac[0] ^ 0x02;
    out[9]  = mac[1];
    out[10] = mac[2];
    out[11] = 0xFF;
    out[12] = 0xFE;
    out[13] = mac[3];
    out[14] = mac[4];
    out[15] = mac[5];
    return out;
}

    __uint128_t ipv6ToByte(std::string ip)
    {
        __uint128_t ipResult = 0;
        unsigned int octets[8] = {0};
        size_t i = 0;
        size_t pos = 0;
        while (i < 8 && (pos = ip.find(':')) != std::string::npos)
        {
            octets[i++] = std::stoi(ip.substr(0, pos), nullptr, 16);
            ip = ip.substr(pos + 1);
        }
        octets[i] = std::stoi(ip, nullptr, 16);
        for (int i = 0; i < 8; ++i)
        {
            ipResult |= static_cast<__uint128_t>(octets[i]) << (112 - i * 16);
        }
        return ipResult;
    }

bool Functions::compareNetworkWithMask(const uint8_t* network, uint8_t mask, AddressFamily af)
{
    size_t maxLen = (af == AddressFamily::IPv4) ? 4 : 16;
    if (mask > maxLen * 8) mask = static_cast<uint8_t>(maxLen * 8);
    size_t fullBytes = mask / 8;
    uint8_t remainingBits = mask % 8;
    if (remainingBits != 0 && fullBytes < maxLen)
    {
        uint8_t maskByte = static_cast<uint8_t>(0xFF >> remainingBits);
        if ((network[fullBytes] & maskByte) != 0) return false;
        ++fullBytes;
    }
    for (size_t i = fullBytes; i < maxLen; ++i)
        if (network[i] != 0) return false;
    return true;
}

uint32_t Functions::findClassfullNetwork(uint32_t ip)
{
    uint32_t ipInt = 0;
    if ((ipInt & 0x80000000) == 0) {
        return ipInt & 0xFF000000;
    }
    else if ((ipInt & 0xC0000000) == 0x80000000) {
        return ipInt & 0xFFFF0000;
    }
    else {
        return ipInt & 0xFFFFFF00;
    }
}

uint8_t Functions::findClassfullNetworkAndMask(uint8_t* out, const uint8_t* ip)
{
    if ((ip[0] & 0x80) == 0) {
        out[0] = ip[0];
        std::memset(out + 1, 0, 3);
        return 24;
    }
    else if ((ip[0] & 0xC0) == 0x80) {
        out[0] = ip[0];
        out[1] = ip[1];
        std::memset(out + 2, 0, 2);
        return 16;
    }
    else {
        out[0] = ip[0];
        out[1] = ip[1];
        out[2] = ip[2];
        out[3] = 0x00;
        return 8;
    }
}

uint8_t Functions::getDefaultMask(uint32_t network)
{
    if ((network & 0x80000000) == 0) // Class A
    {
        return 8;
    }
    else if ((network & 0xC0000000) == 0x80000000) // Class B
    {
        return 16;
    }
    else // Class C
    {
        return 24;
    }
}

bool Functions::validateMacAddress(const uint8_t* mac, const uint8_t* currentMac)
{
    if (mac == currentMac) return true;
    return (mac[0] & 0x01) != 0;
}

bool Functions::isSubnetOf(const uint8_t* network, uint8_t mask, const uint8_t* summaryNetwork, uint8_t summaryMask, AddressFamily af)
{
    if (summaryMask > mask) return false;
    size_t maxLen = (af == AddressFamily::IPv4) ? 4 : 16;
    if (summaryMask > maxLen * 8) summaryMask = static_cast<uint8_t>(maxLen * 8);
    size_t fullBytes = summaryMask / 8;
    uint8_t remainingBits = summaryMask % 8;
    for (size_t i = 0; i < fullBytes; ++i)
    {
        if (network[i] != summaryNetwork[i]) return false;
    }
    if (remainingBits > 0 && fullBytes < maxLen)
    {
        uint8_t maskByte = static_cast<uint8_t>(0xFF << (8 - remainingBits));
        if ((network[fullBytes] & maskByte) != (summaryNetwork[fullBytes] & maskByte))
            return false;
    }
    return true;
}

bool Functions::isMulticast(const uint8_t* ip, AddressFamily af)
{
    if (af == AddressFamily::IPv4)
    {
        return (ip[0] & 0xE0) == 0xE0;
    }
    else if (af == AddressFamily::IPv6)
    {
        return ip[0] == 0xFF;
    }
    return false;
}

std::string Functions::expandIPv6Address(const std::string& ipv6Address) 
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

bool Functions::isIPv6Address(const std::string& address) 
{
    std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))");
    return std::regex_match(address, ipRegex);
}

bool Functions::isIPv6AddressWithMask(const std::string& addressWithMask) 
{
    std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))/(12[0-8]|1[01][0-9]|[1-9]?[0-9])");
    return std::regex_match(addressWithMask, ipRegex);
}

bool Functions::isMACAddress(const std::string& macAddress) 
{
    std::regex macRegex("^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$");
    return std::regex_match(macAddress, macRegex);
}

std::vector<std::string> Functions::tokenize(const std::string& input, char delimiter) 
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

std::string Functions::padWithZeros(const std::string& input) 
{
    std::ostringstream paddedStream;
    paddedStream << std::setfill('0') << std::setw(4) << input;
    return paddedStream.str();
}

bool Functions::isLocalLink(const uint8_t* input)
{
    return input[0] == 0xFE && (input[1] & 0xC0) == 0x80;
}

bool Functions::isLocalLink(__uint128_t input)
{
    return (input >> 118) == 0b1111111010;
}

bool Functions::isGlobalUnicast(const uint8_t* input)
{
    return input[0] == 0xFD;
}

bool Functions::isGlobalUnicast(__uint128_t input)
{
    return (input >> 120) == 0xFD;
}

bool Functions::isLocalUnicast(const uint8_t* input)
{
    return (input[0] &0xE0) == 0x20;
}

bool Functions::isLocalUnicast(__uint128_t input)
{
    return (input >> 125) == 0b001;
}

uint8_t* Functions::prefixToMask(uint8_t* out, uint8_t prefixLen, AddressFamily af)
{
    uint8_t outLen = static_cast<uint8_t>(af);
    if (prefixLen > outLen * 8)
        throw std::invalid_argument("Prefix length exceeds buffer size");

    std::memset(out, 0, outLen);

    size_t fullBytes = prefixLen / 8;
    uint8_t remainingBits = prefixLen % 8;

    for (size_t i = 0; i < fullBytes; ++i)
        out[i] = 0xFF;
    if (remainingBits > 0 && fullBytes < outLen)
        out[fullBytes] = static_cast<uint8_t>(0xFF << (8 - remainingBits));
    return out;
}
    
size_t Functions::getRandomBetween(size_t min, size_t max) {
    if (min > max) std::swap(min, max);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(static_cast<int>(min), static_cast<int>(max));
    return static_cast<size_t>(dis(gen));
}

std::string Functions::timeToString(const std::chrono::system_clock::time_point time)
{
    std::time_t time_t_value = std::chrono::system_clock::to_time_t(time);
    return std::to_string(time_t_value);
}
    
std::string Functions::generateRandomString(size_t length)
{
    static const std::string chars = 
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<> dist(0, chars.size() - 1);

    std::string result;
    result.reserve(length);

    for (size_t i = 0; i < length; ++i)
        result += chars[dist(rng)];

    return result;
}
