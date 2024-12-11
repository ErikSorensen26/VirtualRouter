#pragma once

#include <pcap.h>
#include <string>
#include <vector>
#include <ByteString.hpp>

class Egress {
public:
    // Constructor
    Egress(const std::string& interface);
    
    // Deconstructor
    ~Egress();

    // Sends a packet from base 256 string
    bool sendPacket(const ByteString& base256Str);

private:

    pcap_t* pcap_handle;

    std::vector<unsigned char> base256ToBytes(const ByteString& base256Str);
};
