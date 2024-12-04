#pragma once

#include <any>

#include "Decapsulation.h"
#include <RoutingTable.h>
#include <Interface.h>

using namespace std;

class Interface;

class ProcessPacket {
public:
    // Constructor with args for debugging
    template <typename... Args>
    ProcessPacket(PacketInfo& packet, string& vrf, Interface* Interface, Args... args) : data{any(args)...}, interface(Interface) { Process(packet, vrf); }
    // Constructor
    ProcessPacket(PacketInfo& packet, string& vrf, Interface* Interface);

    // Process Packet
    void Process(PacketInfo& packet, string& vrf);
    // Prints packet for debugging
    bool Print(any param);

private:

    Variable variable;

    Interface* interface;
    string currentVrf;
    
    // Vector for printing
    vector<any> data{};
};
