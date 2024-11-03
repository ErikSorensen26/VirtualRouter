#pragma once
#include <iostream>
#include <fstream>
#include <pcap.h>
#include <string>
#include <iomanip>
#include <mutex>

#include "Que.h"

extern std::mutex packetQueueMutex;

class Ingress {
public:
    // Constructor
    Ingress(const std::string& device, const std::string mask, const int inQueSize, std::string MacAddress);

    // Deconstructor
    ~Ingress();

    // Initiates capture
    int startCapture(const char* filter_exp);

    // Stops capture
    void stopSnif();

    // Packet que
    RingBuffer<std::string> packetQueue;
private:
    std::string localMac;

    pcap_t* pcap_handle;
    bpf_u_int32 subnet;

    static void packetHandler(u_char* user, const struct pcap_pkthdr* pkthdr, const u_char* packet);
    bpf_u_int32 hexStringToNetmask(const std::string& hexString);
};