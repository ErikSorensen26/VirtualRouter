// Ethernet.cpp

#include "packet/PacketStructure.h"
#include "interface/Interface.h"
#include "processing/PacketBuilder.hpp"

namespace infrastructure::ethernet
{
bool build(interface::Interface* iface, processing::PacketBuilder& packetInfo, uint64_t destMac, uint16_t type)
{
    // Create Header
    packet::EthernetHeader ethernetHeader;

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

bool reserve(processing::PacketBuilder& packetInfo)
{
    return (packetInfo.reserveHeader(packet::HeaderType::ETHERNET, packet::EthernetHeader::fixedSize));
}
} // namespace infrastructure::ethernet
