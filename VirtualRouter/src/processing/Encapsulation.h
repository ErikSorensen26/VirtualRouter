/**
 * @file Encapsulation.h
 */

// Encapsulation.h

#ifndef ENCAPSULATION_H
#define ENCAPSULATION_H

#include "packet/PacketStructure.h"

namespace processing
{

class PacketBuilder;
using HeaderType = packet::HeaderType;

/**
 * @brief Serialises the builder's parsed headers into its wire-format frame.
 *
 * This function serializes various protocol headers from the `PacketInfo` structure,
 * recalculates necessary checksums, and appends the encapsulated payload to form a
 * complete packet. It handles multiple protocol layers, including Layer 2 (Ethernet, PPP),
 * Layer 2.5 (ARP, MPLS, VLAN, LLDP), Layer 3 (IPv4, IPv6, GRE, AH, ESP, ICMP, IGMP, EIGRP),
 * Layer 4 (TCP, UDP), and Layer 5 (DHCP).
 *
 * @param packet Builder holding the parsed protocol headers; written in place.
 * @return True if the frame was fully encapsulated, false on an unsupported
 *         header combination or insufficient frame space.
 */
bool encapsulate(PacketBuilder& packet);

} // namespace processing

#endif // ENCAPSULATION_H

