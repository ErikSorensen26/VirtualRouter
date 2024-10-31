#pragma once

#include <pcap.h>
#include <iostream>
#include <string>
#include <vector>

class Egress {
public:
    // Constructor
    Egress(const std::string& interface);
    
    // Deconstructor
    ~Egress();

    // Sends a packet from base 256 string
    bool sendPacket(const std::string& base256Str);

private:

    pcap_t* pcap_handle;

    std::vector<unsigned char> base256ToBytes(const std::string& base256Str);
};