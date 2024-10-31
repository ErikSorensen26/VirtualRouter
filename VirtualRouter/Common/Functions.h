// Functions.h

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

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

//#include <PacketStructure.h>

//Functions* function = Functions::getInstance();

class Functions {
public:
    static Functions* getInstance();
    
    // check for binary
    bool isBinary(const std::string& str);
    // check for hex
    bool isHex(const std::string& str);
    // converts 0 and 1 to bool
    bool stringToBool(const std::string& str);
    // converts binary to hex
    std::string binToHex(const std::string& binaryStr);
    // converts single hex digit to binary
    std::string hexDigitToBin(char hexDigit);
    // converts hex to binary
    std::string hexToBin(const std::string& hexStr);
    // converts binary to bytes
    std::string binToByte(const std::string& binaryData);
    // converts hex to bytes
    static std::string hexToByte(const std::string& hexBinaryData);
    // converts byte to hex
    std::string byteToHex(const std::string& input);
    // converts a char to hex
    std::string charToHex(unsigned char byte);
    // converts a char to binary
    std::string charToBin(unsigned char byte);
    // converts bytes to binary
    std::string byteToBin(const std::string& input);
    // converts binary to decimal
    uint8_t binToDec(const std::string& binary);
    // extracts words into a vector based on spaces
    std::vector<std::string> extractWords(const std::string& str);
    // prints a vector
    void printVector(const std::vector<std::string>& vec);
    // converts to lowercase
    std::string lowerCase(std::string str);
    // converts a string to a number
    int stringToNum(std::string num);
    // converts 
    std::string byteArrayToHex(const std::vector<uint8_t>& byte_array);
    // converts hex to decimal number
    int hexToNum(const std::string& hexStr);
    // converts int to hex
    std::string intToHex(int num);
    // converts int to hex with given byte size
    std::string intToHexWithByte(uint32_t num, int byteSize);
    // converts a subnet mask/ip to hex
    std::string addressToHex(const std::string& mask);
    // converts a ip address
    std::string getIPAddress(const std::string& ip);
    // converts a mac address
    std::string getMacAddress(const std::string& mac);
    // bool to string
    std::string boolToString(bool bol);
    // get random value between
    int getRandomBetween(int min, int max);
    // convert byte to int
    int byteToNum(std::string str);
    // convert int to byte
    std::string intToByte(int num);
    // convert hex mask to int
    int hexMaskToInt(const std::string& mask);
    // compute network address
    std::string computeNetworkAddress(const std::string& ipAddress, int mask);
    // convert int mask to hex
    std::string intMaskToBin(int mask);
    // change size of string
    std::string changeSize(std::string str, int size, std::string value = "0");
private:
    Functions();
    static Functions* singleton;

public:
    Functions(const Functions&) = delete;
    void operator=(const Functions&) = delete;
};

#endif
