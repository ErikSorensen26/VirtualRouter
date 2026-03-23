// Ethernet.h

#ifndef ETHERNET_H
#define ETHERNET_H

#include <cstdint>

namespace interface { class Interface; }
namespace processing { class PacketBuilder; }

namespace infrastructure::ethernet
{
    // Constructs the Ethernet header in PacketInfo based on destination IP
    // Returns true of Ethernet Header was successfully set
    bool build(interface::Interface* iface, processing::PacketBuilder& packetInfo, uint64_t destMac, uint16_t type);

    bool reserve(processing::PacketBuilder& packetInfo);
} // namespace infrastructure::ethernet

#endif // ETHERNET_H

