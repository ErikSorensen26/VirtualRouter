// PacketStructure.h

#ifndef PACKET_STRUCTURE_H
#define PACKET_STRUCTURE_H

#include <cstdint>
#include <cstddef>

#include "packet/headers/EthernetHeader.hpp"
#include "packet/headers/ArpHeader.hpp"
#include "packet/headers/MplsHeader.hpp"
#include "packet/headers/IpHeaders.hpp"
#include "packet/headers/TcpHeader.hpp"
#include "packet/headers/UdpHeader.hpp"
#include "packet/headers/IcmpHeader.hpp"
#include "packet/headers/Icmpv6Header.hpp"
#include "packet/headers/AhHeader.hpp"
#include "packet/headers/EspHeader.hpp"
#include "packet/headers/DhcpHeader.hpp"
#include "packet/headers/Dhcpv6Header.hpp"
#include "packet/headers/Dhcpv6RelayHeader.hpp"
#include "packet/headers/EigrpHeader.hpp"
#include "packet/headers/Ospfv2Header.hpp"
#include "packet/headers/Ospfv3Header.hpp"
#include "packet/headers/BgpHeader.hpp"
#include "packet/headers/SyslogHeader.hpp"

static constexpr int MaxHeaders = 16;

/**
 * @file Encapsulation.h
 * @brief Defines constants, structures, and utility functions for packet encapsulation.
 */
namespace Variable
{
    
    /**
     * @namespace IP
     * @brief Contains IP protocol number constants.
     */
    namespace IP
    {
        // Additional Protocols
        inline constexpr uint8_t hopopt = uint8_t{0x00U};       ///< IP protocol number for HOPOPT (0).
        inline constexpr uint8_t ggp = uint8_t{0x03U};          ///< IP protocol number for GGP (3).
        inline constexpr uint8_t ipv4 = uint8_t{0x04U};         ///< IP protocol number for IPv4 (4).
        inline constexpr uint8_t st = uint8_t{0x05U};           ///< IP protocol number for ST (5).
        inline constexpr uint8_t cbt = uint8_t{0x07U};          ///< IP protocol number for CBT (7).
        inline constexpr uint8_t egp = uint8_t{0x08U};          ///< IP protocol number for EGP (8).
        inline constexpr uint8_t igp_v2 = uint8_t{0x09U};       ///< IP protocol number for IGP (9).
        inline constexpr uint8_t bbn_rcc_mon = uint8_t{0x0aU};  ///< IP protocol number for BBN_RCC_MON (10).
        inline constexpr uint8_t nvp_ii = uint8_t{0x0bU};        ///< IP protocol number for NVP-II (11).
        inline constexpr uint8_t pup = uint8_t{0x0cU};           ///< IP protocol number for PUP (12).
        inline constexpr uint8_t argus = uint8_t{0x0dU};         ///< IP protocol number for ARGUS (13).
        inline constexpr uint8_t emcon = uint8_t{0x0eU};         ///< IP protocol number for EMCON (14).
        inline constexpr uint8_t xnet = uint8_t{0x0fU};          ///< IP protocol number for XNET (15).
        inline constexpr uint8_t chaos = uint8_t{0x10U};         ///< IP protocol number for CHAOS (16).
        inline constexpr uint8_t mux = uint8_t{0x12U};           ///< IP protocol number for MUX (18).
        inline constexpr uint8_t dcn_meas = uint8_t{0x13U};      ///< IP protocol number for DCN_MEAS (19).
        inline constexpr uint8_t hmp = uint8_t{0x14U};           ///< IP protocol number for HMP (20).
        inline constexpr uint8_t prm = uint8_t{0x15U};           ///< IP protocol number for PRM (21).
        inline constexpr uint8_t xnsIdp = uint8_t{0x16U};       ///< IP protocol number for XNS_IDP (22).
        inline constexpr uint8_t trunk1 = uint8_t{0x17U};       ///< IP protocol number for TRUNK-1 (23).
        inline constexpr uint8_t trunk2 = uint8_t{0x18U};       ///< IP protocol number for TRUNK-2 (24).
        inline constexpr uint8_t leaf1 = uint8_t{0x19U};        ///< IP protocol number for LEAF-1 (25).
        inline constexpr uint8_t leaf2 = uint8_t{0x1aU};        ///< IP protocol number for LEAF-2 (26).
        inline constexpr uint8_t rdp = uint8_t{0x1bU};           ///< IP protocol number for RDP (27).
        inline constexpr uint8_t irtp = uint8_t{0x1cU};          ///< IP protocol number for IRTP (28).
        inline constexpr uint8_t isoTp4 = uint8_t{0x1dU};       ///< IP protocol number for ISO_TP4 (29).
        inline constexpr uint8_t netblt = uint8_t{0x1eU};        ///< IP protocol number for NETBLT (30).
        inline constexpr uint8_t mfeNsp = uint8_t{0x1fU};       ///< IP protocol number for MFE_NSP (31).
        inline constexpr uint8_t meritInp = uint8_t{0x20U};     ///< IP protocol number for MERIT_INP (32).
        inline constexpr uint8_t dccp = uint8_t{0x21U};          ///< IP protocol number for DCCP (33).
        inline constexpr uint8_t ipcomp = uint8_t{0x22U};        ///< IP protocol number for IPCOMP (34).
        inline constexpr uint8_t eigrpv6 = uint8_t{0x58U};      ///< IP protocol number for EIGRP (88).
        inline constexpr uint8_t ospfv3 = uint8_t{0x59U};       ///< IP protocol number for OSPF (89).
        inline constexpr uint8_t pimSm = uint8_t{0x67U};        ///< IP protocol number for PIM-SM (103).
        inline constexpr uint8_t l2tp = uint8_t{0x4eU};          ///< IP protocol number for L2TP (78).
        inline constexpr uint8_t mplsInIp = uint8_t{0x2bU};    ///< IP protocol number for MPLS-in-IP (43).
        inline constexpr uint8_t vxlan = uint8_t{0xb7U};         ///< IP protocol number for VXLAN (183).
        inline constexpr uint8_t dvrp = uint8_t{0x5eU};          ///< IP protocol number for DVRP (94).
        inline constexpr uint8_t lmtp = uint8_t{0x46U};          ///< IP protocol number for LMTP (70).
        inline constexpr uint8_t encap = uint8_t{0x8eU};         ///< IP protocol number for ENCAPSULATION (142).
        inline constexpr uint8_t ipv6 = uint8_t{0x29U};          ///< IP protocol number for IPv6 (41).
        inline constexpr uint8_t pimDm = uint8_t{0x64U};        ///< IP protocol number for PIM-DM (100).
        inline constexpr uint8_t eigrpv5 = uint8_t{0x8aU};      ///< IP protocol number for EIGRP for IPv6 (138).
        inline constexpr uint8_t rsvp_te = uint8_t{0x73U};       ///< IP protocol number for RSVP-TE (115).
        inline constexpr uint8_t mpls = uint8_t{0x2bU};          ///< IP protocol number for MPLS (43). Note: MPLS has multiple entries.
        inline constexpr uint8_t pppoeDiscovery = uint8_t{0x11U}; ///< IP protocol number for PPPoE Discovery (17).
        inline constexpr uint8_t pppoeSession = uint8_t{0x11U};   ///< IP protocol number for PPPoE Session (17).
        inline constexpr uint8_t pppEcho = uint8_t{0x01U};        ///< IP protocol number for PPP Echo (1).
        inline constexpr uint8_t pppIpcp = uint8_t{0x21U};        ///< IP protocol number for PPP IPCP (33).
        inline constexpr uint8_t pppIpv6cp = uint8_t{0x57U};      ///< IP protocol number for PPP IPv6CP (87).
        inline constexpr uint8_t eap = uint8_t{0x88U};             ///< IP protocol number for EAP (136).
        inline constexpr uint8_t lispControl = uint8_t{0x8fU};    ///< IP protocol number for LISP Control (143).
        inline constexpr uint8_t mobileregistrationProtocol = uint8_t{0x8dU}; ///< IP protocol number for Mobile Registration Protocol (141).
    }
}

// ------------------------- Structure Definitions -------------------------

enum class HeaderType : uint8_t
{
    NONE,
    ETHERNET,
    ARP, MPLS,
    IPV4, IPV6, AH, ESP, ICMP, ICMPV6,
    TCP, UDP, EIGRP, OSPFV2, OSPFV3,
    DHCP, DHCPV6, DHCPV6_RELAY, BGP,
    ENCAPSULATE
};

enum class HeaderLayer
{
    LAYER2,
    LAYER2_5,
    LAYER3,
    LAYER4,
    LAYER5
};

inline size_t getHeaderSize(HeaderType type)
{
    switch (type)
    {
        case HeaderType::ETHERNET: return EthernetHeader::fixedSize;
        case HeaderType::ARP: return ArpHeader::fixedSize;
        case HeaderType::MPLS: return MplsHeader::fixedSize;
        case HeaderType::IPV4: return IPv4Header::fixedSize;
        case HeaderType::IPV6: return IPv6Header::fixedSize;
        case HeaderType::AH: return AhHeader::fixedSize;
        case HeaderType::ESP: return EspHeader::fixedSize;
        case HeaderType::ICMP: return IcmpHeader::fixedSize;
        case HeaderType::ICMPV6: return Icmpv6Header::fixedSize;
        case HeaderType::TCP: return TcpHeader::fixedSize;
        case HeaderType::UDP: return UdpHeader::fixedSize;
        case HeaderType::EIGRP: return EigrpHeader::fixedSize;
        case HeaderType::OSPFV2: return Ospfv2Header::fixedSize;
        case HeaderType::OSPFV3: return Ospfv3Header::fixedSize;
        case HeaderType::DHCP: return DhcpHeader::fixedSize;
        case HeaderType::DHCPV6: return Dhcpv6Header::fixedSize;
        case HeaderType::DHCPV6_RELAY: return Dhcpv6RelayHeader::fixedSize;
        case HeaderType::BGP: return BgpHeader::fixedSize;
        default: return 0;
    }
}

struct HeaderEntry
{
    HeaderType type;
    size_t offset;
    size_t size;
};

struct PacketInfo
{
    HeaderEntry headers[MaxHeaders];
    uint8_t count = 0;
    size_t offset = 0;
};

#endif // PACKET_STRUCTUrE_H
