// Encapsulation.h

#ifndef ENCAPSULATION_H
#define ENCAPSULATION_H

#include <Checksums.h>
#include <ByteString.hpp>
#include <Process.h>
#include <optional>

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
std::optional<ByteString> encapsulate(PacketInfo& packet, ByteString encapsulated = "");

// Enum representing every header
enum class HeaderType
{
    // Headers
    Ethernet, Ppp, Frame,
    Arp, Mpls, Vlan, Lldp,
    IPv4, IPv6, Gre, Ah, Esp, Icmp, Icmpv6, Igmp,
    Tcp, Udp, Eigrp,
    Dhcp, Dhcpv6, Dhcpv6Relay,

    // Count
    Count,
};

// Maps the header to HeaderType (Can be used for indexing headers)
inline HeaderType mapHeaderToEnum(const EthernetHeader&) { return HeaderType::Ethernet; }
inline HeaderType mapHeaderToEnum(const PppHeader&) { return HeaderType::Ppp; }
inline HeaderType mapHeaderToEnum(const FrameHeader&) { return HeaderType::Frame; }

inline HeaderType mapHeaderToEnum(const ArpHeader&) { return HeaderType::Arp; }
inline HeaderType mapHeaderToEnum(const MplsHeader&) { return HeaderType::Mpls; }
inline HeaderType mapHeaderToEnum(const VlanHeader&) { return HeaderType::Vlan; }
inline HeaderType mapHeaderToEnum(const LldpHeader&) { return HeaderType::Lldp; }

inline HeaderType mapHeaderToEnum(const IPv4Header&) { return HeaderType::IPv4; }
inline HeaderType mapHeaderToEnum(const IPv6Header&) { return HeaderType::IPv6; }
inline HeaderType mapHeaderToEnum(const GreHeade&) { return HeaderType::Gre; }
inline HeaderType mapHeaderToEnum(const AhHeader&) { return HeaderType::Ah; }
inline HeaderType mapHeaderToEnum(const EspHeader&) { return HeaderType::Esp; }
inline HeaderType mapHeaderToEnum(const IcmpHeader&) { return HeaderType::Icmp; }
inline HeaderType mapHeaderToEnum(const IcmpV6Header&) { return HeaderType::Icmpv6; }
inline HeaderType mapHeaderToEnum(const IgmpHeader&) { return HeaderType::Igmp; }

inline HeaderType mapHeaderToEnum(const TcpHeader&) { return HeaderType::Tcp; }
inline HeaderType mapHeaderToEnum(const UdpHeader&) { return HeaderType::Udp; }
inline HeaderType mapHeaderToEnum(const EigrpHeader&) { return HeaderType::Eigrp; }

inline HeaderType mapHeaderToEnum(const DhcpHeader&) { return HeaderType::Dhcp; }
inline HeaderType mapHeaderToEnum(const Dhcpv6Header&) { return HeaderType::Dhcpv6; }
inline HeaderType mapHeaderToEnum(const Dhcpv6RelayHeader&) { return HeaderType::Dhcpv6Relay; }

#endif // ENCAPSULATION_H
