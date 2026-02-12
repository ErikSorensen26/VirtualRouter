// Ethernet.cpp

#include "packet/PacketStructure.h"
#include "interface/Interface.h"
#include "processing/PacketBuilder.hpp"

namespace Protocol::Ethernet
{
bool build(Interface* iface, PacketBuilder& packetInfo, const uint8_t* destMac, uint16_t type)
{
    // Create Header
    EthernetHeader ethernetHeader;

    auto* nextHeader = packetInfo.nextBuildHeader();
    if (!nextHeader) return false;

    ethernetHeader.setBuffer(nextHeader->buffer);

    // Set type and destination
    ethernetHeader.setType(type);
    iface->configs.getMac(ethernetHeader.raw->sourceMac);
    ethernetHeader.setDestinationMac(destMac);
    
    iface->enqueuePacket(packetInfo);

    return true;
}

bool reserve(PacketBuilder& packetInfo)
{
    return (packetInfo.reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize));
}
} // Namespace Protocol::Ethernet
