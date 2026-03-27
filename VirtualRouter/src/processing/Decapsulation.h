/**
 * @file Decapsulation.h
 */

// Decapsulation.h

#ifndef DECAPSULATION_H
#define DECAPSULATION_H

#include <cstdint>
#include <cstddef>

#include "packet/PacketStructure.h"

namespace processing
{

using PacketInfo = packet::PacketInfo;

/**
 * @class Packet
 * @brief Represents a network packet and handles its decapsulation across various protocol layers.
 *
 * The Packet class is responsible for inspecting and decapsulating network packets through layers 2 to 5.
 * It parses headers for protocols such as Ethernet, ARP, IPv4/IPv6, TCP/UDP, ICMP/ICMPv6, IGMP, TLS,
 * MPLS, GRE, PPP, Frame Relay, AH, ESP, VLAN, LLDP, DHCP, EIGRP, SysLog, ctr..
 *
 * It utilizes the Singleton design pattern to ensure a single instance per packet processing.
 * Thread safety is maintained through the use of mutexes and proper synchronization mechanisms.
 */
bool inspect(PacketInfo& packet, uint8_t* data, size_t len);
bool decapsulate(PacketInfo& packet, uint8_t* data, size_t len);

} // namespace processing

#endif // DECAPSULATION_H

