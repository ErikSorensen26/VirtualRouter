#pragma once
#include <pcap.h>
#include <string>
#include <mutex>
#include <Functions.h>
#include <ByteString.hpp>

#include "Que.h"

extern std::mutex packetQueueMutex;

class Ingress {
public:
    // Constructor
    Ingress(const std::string& device, const std::string mask, const int inQueSize);

    // Deconstructor
    ~Ingress();

    // Initiates capture
    int startCapture(const char* filter_exp);

    // Stops capture
    void stopSnif();

    // Packet que
    RingBuffer<ByteString> packetQueue;
private:
    pcap_t* pcap_handle;
    bpf_u_int32 subnet;

    static void packetHandler(u_char* user, const struct pcap_pkthdr* pkthdr, const u_char* packet);
};
