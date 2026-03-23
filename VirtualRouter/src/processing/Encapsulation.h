// Encapsulation.h

#ifndef ENCAPSULATION_H
#define ENCAPSULATION_H

#include "packet/PacketStructure.h"

namespace processing
{

class PacketBuilder;
using HeaderType = packet::HeaderType;

/**
 * @file Encapsulation.cpp
 * @brief Implements the encapsulation of packet information into a formatted ByteString.
 */

/**
 * @brief Encapsulates packet information into a formatted ByteString.
 *
 * This function serializes various protocol headers from the `PacketInfo` structure,
 * recalculates necessary checksums, and appends the encapsulated payload to form a
 * complete packet. It handles multiple protocol layers, including Layer 2 (Ethernet, PPP),
 * Layer 2.5 (ARP, MPLS, VLAN, LLDP), Layer 3 (IPv4, IPv6, GRE, AH, ESP, ICMP, IGMP, EIGRP),
 * Layer 4 (TCP, UDP), and Layer 5 (DHCP).
 *
 * @param packet A reference to a `PacketInfo` structure containing parsed protocol headers.
 * @param encapsulated A `ByteString` representing the encapsulated payload (e.g., application data).
 * @return A `ByteString` containing the fully encapsulated and formatted packet ready for transmission.
 */
bool encapsulate(PacketBuilder& packet);

} // namespace processing

#endif // ENCAPSULATION_H

