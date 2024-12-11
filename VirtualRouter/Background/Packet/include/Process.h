#pragma once

#include <any>
#include <RoutingTable.h>
#include <Interface.h>
#include <ByteString.hpp>
#include <Que.h>

class Interface;

class ProcessPacket {
public:
    // Constructor with args for debugging
    template <typename... Args>
    ProcessPacket(PacketInfo& packet, ByteString& vrf, Interface* Interface, Args... args) : data{std::any(args)...}, interface(Interface) { process(packet, vrf); }
    // Constructor
    ProcessPacket(PacketInfo& packet, ByteString& vrf, Interface* Interface);

    // Process Packet
    void process(PacketInfo& packet, ByteString& vrf);
    // Prints packet for debugging
    bool print(std::any param);

private:

    Interface* interface;
    ByteString currentVrf;
    
    // Vector for printing
    std::vector<std::any> data{};
};
