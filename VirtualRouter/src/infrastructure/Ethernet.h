// Ethernet.h

#ifndef ETHERNET_H
#define ETHERNET_H

#include <cstdint>

class Interface;
class PacketBuilder;

namespace Protocol::Ethernet
{
    // Constructs the Ethernet header in PacketInfo based on destination IP
    // Returns true of Ethernet Header was successfully set
    bool build(Interface* iface, PacketBuilder& packetInfo, uint64_t destMac, uint16_t type);

    bool reserve(PacketBuilder& packetInfo);
} // Namespace Protocol::Ethernet

#endif // ETHERNET_H
