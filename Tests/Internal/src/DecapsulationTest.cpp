#include <gtest/gtest.h>
#include "Decapsulation.h"      // Your Packet class
#include "PacketStructure.h"    // All your protocol structs

//--------------------------------------------------------------------------------
// Test Fixture
//--------------------------------------------------------------------------------

class Internal_DecapsulationTest : public ::testing::Test 
{
protected:
    PacketInfo pkt;

    bool inspection(PacketInfo& pkt, uint8_t* packet, size_t size)
    {
        if (inspect(pkt, packet, size) && decapsulate(pkt, packet, size))
        {
            return true;
        }
        return false;
    }
};

//
// Below, we do two tests (valid & invalid) per protocol. Each "valid" test
// includes at least the required L2 + optional L2.5 + L3 + L4... needed
// to realistically reach that protocol or show that protocol's presence.
//
// We check the correct `std::any` type in the corresponding packetInfo.LayerX.
//

//--------------------------------------------------------------------------------
// ETHERNET (L2) + ARP (L2.5)
//--------------------------------------------------------------------------------

// Test EthernetArp_Valid
TEST_F(Internal_DecapsulationTest, EthernetArp_Valid)
{
    // Build a minimal Ethernet + ARP "whole packet":
    // [Ethernet(14)] + [ARP(28)] = 42 bytes
    alignas(64) uint8_t packet[42] = {
       // Ethernet: 6 dst + 6 src + 2 type=0x0806(ARP)
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // dst mac
       0x11, 0x11, 0x22, 0x33, 0x44, 0x55,  // src mac
       0x08, 0x06,                  // EtherType = ARP
       // ARP (28 bytes):
       0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01,  // htype=1, ptype=0x0800, hlen=6, plen=4, opcode=1
       0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,          // Sender MAC
       0xC0, 0xA8, 0x01, 0x01,                  // Sender IP
       0x11, 0x22, 0x33, 0x44, 0x55, 0x66,          // Target MAC
       0xC0, 0xA8, 0x01, 0x02,                 // Target IP
    };

    EXPECT_TRUE(inspection(pkt, packet, 42)); // Decapsulate everything

    // Check L2: Ethernet
    bool hasEth = false;
    bool hasArp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
        {
            if (hasEth) FAIL();
            hasEth = true;
        }
        else if (pkt.headers[i].type == HeaderType::ARP)
        {
            if (hasArp) FAIL();
            hasArp = true;
        }
        else
            FAIL();
    }
    EXPECT_TRUE(hasEth);
    EXPECT_TRUE(hasArp);
}

// Test EthernetArp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetArp_Invalid)
{
    // Make it too short to contain ARP (only 20 bytes total)
    // Ethernet alone needs 14 bytes, so ARP can't fit
    alignas(64) uint8_t packet[18] = {
        // Ethernet: 6 dst + 6 src + 2 type=0x0806(ARP)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // dst mac
        0x11, 0x11, 0x22, 0x33, 0x44, 0x55,  // src mac
        0x08, 0x06,                  // EtherType = ARP
        // Invlaid Arp
        0xFF, 0xFF, 0xFF, 0xFF
    };

    EXPECT_FALSE(inspection(pkt, packet, 18));

    // Likely we can parse Ethernet (because we do have 14 bytes),
    // but there's not enough for ARP (28 needed).
    bool hasEth = false;
    bool hasArp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
            hasEth = true;
        if (pkt.headers[i].type == HeaderType::ARP)
            hasArp = true;
    }
    EXPECT_TRUE(hasEth);
    EXPECT_FALSE(hasArp);
}

//--------------------------------------------------------------------------------
// ETHERNET + MPLS + IPv4
//   (Pretend minimal MPLS label, then an IPv4 header. Real networks might do more.)
//--------------------------------------------------------------------------------

// Test EthernetMplsIPv4_Valid
TEST_F(Internal_DecapsulationTest, EthernetMplsIPv4_Valid)
{
    // Ethernet(14) + MPLS(4) + minimal IPv4(20) = 38 bytes
    alignas(64) uint8_t packet[38] = {
       // Ethernet
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
       0x11, 0x11, 0x22, 0x33, 0x44, 0x55,
       0x88, 0x47,   // EtherType for MPLS unicast
       // MPLS (4 bytes)
       0x00, 0x01, 0x11, 0xFF, // Label=0x0001, EXP=0, S=1, TTL=255
       // Minimal IPv4 (20 bytes)
       0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x40, 0x00, 0x40, 0x00, 0x00, 0x00,
       0xC0, 0xA8, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x02
    };

    ASSERT_TRUE(inspection(pkt, packet, 38));

    bool hasEth = false;
    bool hasMpls = false;
    bool hasIPv4 = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
        {
            if (hasEth) FAIL();
            hasEth = true;
        }
        else if (pkt.headers[i].type == HeaderType::MPLS)
        {
            if (hasMpls) FAIL();
            hasMpls = true;
        }
        else if (pkt.headers[i].type == HeaderType::IPV4)
        {
            if (hasIPv4) FAIL();
            hasIPv4 = true;
        }
        else
            FAIL();
    }
    ASSERT_TRUE(hasEth);
    ASSERT_TRUE(hasMpls);
    ASSERT_TRUE(hasIPv4);
}

// Test EthernetMplsIPv4_Invalid
TEST_F(Internal_DecapsulationTest, EthernetMplsIPv4_Invalid)
{
    // We'll have Ethernet(14) + MPLS(4) but only 10 bytes for IPv4 => insufficient
    alignas(64) uint8_t packet[14 + 4 + 10] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x88, 0x47,
        0x00, 0x01, 0x10, 0xFF, // MPLS
        // Only 10 bytes for supposed IPv4 => incomplete
        0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x40, 0x00, 0x40, 0x06,
    };

    ASSERT_FALSE(inspection(pkt, packet, 14 + 4 + 10));

    bool hasEth = false;
    bool hasMpls = false;
    bool hasIPv4 = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
            hasEth = true;
        else if (pkt.headers[i].type == HeaderType::MPLS)
            hasMpls = true;
        else if (pkt.headers[i].type == HeaderType::IPV4)
            hasIPv4 = true;
    }
    ASSERT_TRUE(hasEth);
    ASSERT_TRUE(hasMpls);
    ASSERT_FALSE(hasIPv4);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv4 + ICMP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Icmp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Icmp_Valid)
{
    // Ethernet(14) + IPv4(20) + ICMP(8) = 42
    alignas(64) uint8_t packet[14 + 20 + 8] = {
       // Ethernet
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
       0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
       0x08, 0x00,    // EtherType=IPv4
       // IPv4 (20 bytes)
       0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x40, 0x01, 0xAA, 0x01, 0x12, 0x34,
       0xC0, 0xA8, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x02,
       // ICMP (8 bytes)
       0x08, 0x00, 0x12, 0x34, 0x00, 0x01, 0x00, 0x02
    };

    ASSERT_TRUE(inspection(pkt, packet, 14 + 20 + 8));

    bool hasEth = false;
    bool hasIPv4 = false;
    bool hasIcmp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
        {
            if (hasEth) FAIL();
            hasEth = true;
        }
        else if (pkt.headers[i].type == HeaderType::IPV4)
        {
            if (hasIPv4) FAIL();
            hasIPv4 = true;
        }
        else if (pkt.headers[i].type == HeaderType::ICMP)
        {
            if (hasIcmp) FAIL();
            hasIcmp = true;
        }
        else
            FAIL();
    }

    ASSERT_TRUE(hasEth);
    ASSERT_TRUE(hasIPv4);
    ASSERT_TRUE(hasIcmp);
}

// Test EthernetIPv4Icmp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Icmp_Invalid)
{
    // We'll do Ethernet(14) + IPv4(20) but only 4 bytes left for ICMP => incomplete
    alignas(64) uint8_t packet[14 + 20 + 4] = {
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
       0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
       0x08, 0x00,
       0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x40, 0x01, 0xAA, 0x01, 0x12, 0x34,
       0xC0, 0xA8, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x02,
       // 4 bytes leftover => incomplete ICMP
       0x08, 0x00, 0x12, 0x34
    };

    ASSERT_FALSE(inspection(pkt, packet, 14 + 20 + 4));

    bool hasEth = false;
    bool hasIPv4 = false;
    bool hasIcmp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
            hasEth = true;
        else if (pkt.headers[i].type == HeaderType::IPV4)
            hasIPv4 = true;
        else if (pkt.headers[i].type == HeaderType::ICMP)
            hasIcmp = true;
    }
    ASSERT_TRUE(hasEth);
    ASSERT_TRUE(hasIPv4);
    ASSERT_FALSE(hasIcmp);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv6 + ICMPv6
//--------------------------------------------------------------------------------

// Test EthernetIPv6Icmpv6_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv6Icmpv6_Valid)
{
    // Ethernet(14) + IPv6(40) + ICMPv6(8) = 62
    alignas(64) uint8_t packet[14 + 40 + 8] = {
       // Ethernet
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
       0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
       0x86, 0xDD, // EtherType for IPv6
       // Minimal IPv6 (40 bytes, NextHeader=58 => ICMPv6)
       0x60, 0x00, 0x00, 0x00, 0x00, 0x08, 0x3A, 0x40,  
       0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
       0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
       // ICMPv6(8 bytes)
       0x80, 0x00, 0x12, 0x34, 0x00, 0x00, 0x00, 0x00
    };

    ASSERT_TRUE(inspection(pkt, packet, 14 + 40 + 8));

    bool hasEth = false;
    bool hasIPv6 = false;
    bool hasICMPv6 = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
        {
            if (hasEth) FAIL();
            hasEth = true;
        }
        else if (pkt.headers[i].type == HeaderType::IPV6)
        {
            if (hasIPv6) FAIL();
            hasIPv6 = true;
        }
        else if (pkt.headers[i].type == HeaderType::ICMPV6)
        {
            if (hasICMPv6) FAIL();
            hasICMPv6 = true;
        }
        else
            FAIL();
    }

    ASSERT_TRUE(hasEth);
    ASSERT_TRUE(hasIPv6);
    ASSERT_TRUE(hasICMPv6);
}

// Test EthernetIPv6Icmpv6_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv6Icmpv6_Invalid)
{
    // Just remove some bytes from ICMPv6 => incomplete
    alignas(64) uint8_t packet[14 + 40 + 4] = {
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
       0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
       0x86, 0xDD,
       0x60, 0x00, 0x00, 0x00, 0x00, 0x08, 0x3A, 0x40,  
       0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
       0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
       // Only 4 bytes for ICMPv6
       0x80, 0x00, 0x12, 0x34
    };

    ASSERT_FALSE(inspection(pkt, packet, 14 + 40 + 4));

    bool hasEth = false;
    bool hasIPv6 = false;
    bool hasICMPv6 = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
            hasEth = true;
        else if (pkt.headers[i].type == HeaderType::IPV6)
            hasIPv6 = true;
        else if (pkt.headers[i].type == HeaderType::ICMPV6)
            hasICMPv6 = true;
    }

    ASSERT_TRUE(hasEth);
    ASSERT_TRUE(hasIPv6);
    ASSERT_FALSE(hasICMPv6);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv4 + TCP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Tcp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Tcp_Valid)
{
    // Ethernet(14) + IPv4(20, proto=6) + TCP(20) => 54
    alignas(64) uint8_t packet[14 + 20 + 20] = {
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x08, 0x00,
      // IPv4 => proto=6 (TCP)
      0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x40, 0x00, 0x11, 0x06, 0x00, 0x00,
      0xC0, 0xA8, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x02,
      // TCP => 20 bytes
      0x1F, 0x90, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x50, 0x02, 0x71, 0x10, 0x12, 0x34, 0x00, 0x00,
    };

    inspection(pkt, packet, 14 + 20 + 20);

    bool hasEth = false;
    bool hasIPv4 = false;
    bool hasTcp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
        {
            if (hasEth) FAIL();
            hasEth = true;
        }
        else if (pkt.headers[i].type == HeaderType::IPV4)
        {
            if (hasIPv4) FAIL();
            hasIPv4 = true;
        }
        else if (pkt.headers[i].type == HeaderType::TCP)
        {
            if (hasTcp) FAIL();
            hasTcp = true;
        }
        else
            FAIL();
    }

    ASSERT_TRUE(hasEth);
    ASSERT_TRUE(hasIPv4);
    ASSERT_TRUE(hasTcp);
}

// Test EthernetIPv4Tcp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Tcp_Invalid)
{
    // Truncate TCP => only 10 bytes
    alignas(64) uint8_t packet[14 + 20 + 10] = {
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x08, 0x00,
      0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x40, 0x00, 0x11, 0x06, 0x00, 0x00,
      0xC0, 0xA8, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x02,
      0x1F, 0x90, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x50, 0x02,
    };

    EXPECT_FALSE(inspection(pkt, packet, 14 + 20 + 10));

    bool hasEth = false;
    bool hasIPv4 = false;
    bool hasTcp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
            hasEth = true;
        else if (pkt.headers[i].type == HeaderType::IPV4)
            hasIPv4 = true;
        else if (pkt.headers[i].type == HeaderType::TCP)
            hasTcp = true;
    }

    EXPECT_TRUE(hasEth);
    EXPECT_TRUE(hasIPv4);
    EXPECT_FALSE(hasTcp);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv6 + TCP
//--------------------------------------------------------------------------------

// Test EthernetIPv6Tcp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv6Tcp_Valid)
{
    GTEST_SKIP();
}

// Test EthernetIPv6Tcp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv6Tcp_Invalid)
{
    GTEST_SKIP();
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv4 + UDP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Udp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Udp_Valid)
{
    // Ethernet(14) + IPv4(20, proto=17 => UDP) + UDP(8)
    alignas(64) uint8_t packet[14 + 20 + 8] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        // IPv4 => proto=17(UDP)
        0x45, 0x00, 0x00, 0x1C,
        0x00, 0x00, 0x40, 0x00, 0x11, 0x11,
        0x00, 0x00,
        0xC0, 0xA8, 0x01, 0x01,
        0xC0, 0xA8, 0x01, 0x02,
        // UDP(8)
        0x1F, 0x90, 0x00, 0x35,
        0x00, 0x08, 0x12, 0x34
    };

    inspection(pkt, packet, 14 + 20 + 8);

    bool hasEth = false;
    bool hasIPv4 = false;
    bool hasUdp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
        {
            if (hasEth) FAIL();
            hasEth = true;
        }
        else if (pkt.headers[i].type == HeaderType::IPV4)
        {
            if (hasIPv4) FAIL();
            hasIPv4 = true;
        }
        else if (pkt.headers[i].type == HeaderType::UDP)
        {
            if (hasUdp) FAIL();
            hasUdp = true;
        }
    }

    EXPECT_TRUE(hasEth);
    EXPECT_TRUE(hasIPv4);
    EXPECT_TRUE(hasUdp);
}

// Test EthernetIPv4Udp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Udp_Invalid)
{
    // Truncated UDP => 4 bytes
    alignas(64) uint8_t packet[14 + 20 + 4] = {
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x08, 0x00,
      0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x40, 0x00, 0x11, 0x11, 0x00, 0x00,
      0xC0, 0xA8, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x02,
      0x1F, 0x90, 0x00, 0x35
    };

    EXPECT_FALSE(inspection(pkt, packet, 14 + 20 + 4));

    bool hasEth = false;
    bool hasIPv4 = false;
    bool hasUdp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
            hasEth = true;
        else if (pkt.headers[i].type == HeaderType::IPV4)
            hasIPv4 = true;
        else if (pkt.headers[i].type == HeaderType::UDP)
            hasUdp = true;
    }

    EXPECT_TRUE(hasEth);
    EXPECT_TRUE(hasIPv4);
    EXPECT_FALSE(hasUdp);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv6 + UDP
//--------------------------------------------------------------------------------

// Test EthernetIPv6Udp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv6Udp_Valid)
{
    GTEST_SKIP();
}

// Test EthernetIPv6Udp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv6Udp_Invalid)
{
    GTEST_SKIP();
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv4 + UDP + DHCP (L5 in your code)
//--------------------------------------------------------------------------------

// Test EthernetIPv4UdpDhcp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv4UdpDhcp_Valid)
{
    // Build a minimal packet with DHCP ports: (src=67,dst=68 or vice versa)
    // We'll keep it short but at least enough that decodeDhcp won't crash
    // Ethernet(14) + IPv4(20, proto=17) + UDP(8) + DHCP(240 bytes?)
    alignas(64) uint8_t packet[14 + 20 + 8 + 236 + 17] = {
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x08, 0x00,
      // IPv4 => proto=17(UDP)
      0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x40, 0x00, 0x11, 0x11, 0x00, 0x00,
      0xC0, 0xA8, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x02,
      // UDP(8)
      0x00, 0x43, 0x00, 0x44, 0x00, 0x08, 0x12, 0x34,
    };

    std::memset(packet + 14 + 20 + 8, 0, 236);

    uint8_t dhcp[17] = {
        0x63, 0x82, 0x52, 0x63, 0x35, 0x01, 0x01, 0x3D,
        0x07, 0x01, 0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E,
        0xff
    };

    std::memcpy(packet + 14 + 20 + 8 + 236, dhcp, 17);

    bool hasEth = false;
    bool hasIPv4 = false;
    bool hasUdp = false;
    bool hasDhcp = false;

    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
        {
            if (hasEth) FAIL();
            hasEth = true;
        }
        else if (pkt.headers[i].type == HeaderType::IPV4)
        {
            if (hasIPv4) FAIL();
            hasIPv4 = true;
        }
        else if (pkt.headers[i].type == HeaderType::UDP)
        {
            if (hasUdp) FAIL();
            hasUdp = true;
        }
        else if (pkt.headers[i].type == HeaderType::DHCP)
        {
            if (hasDhcp) FAIL();
            hasDhcp = true;
        }
        else
            FAIL();
    }
}

// Test EthernetIPv4UdpDhcp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4UdpDhcp_Invalid)
{
    // Not enough data for minimal DHCP => only 100 bytes

    alignas(64) uint8_t packet[14 + 20 + 8 + 100] = {
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x08, 0x00,
      // IPv4 => proto=17(UDP)
      0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x40, 0x00, 0x11, 0x11, 0x00, 0x00,
      0xC0, 0xA8, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x02,
      // UDP(8)
      0x00, 0x43, 0x00, 0x44, 0x00, 0x08, 0x12, 0x34,
    };
    // DHCP => 240 bytes
    std::memset(packet + 14 + 20 + 8, 0x11, 100);

    EXPECT_FALSE(inspection(pkt, packet, 14 + 20 + 8 + 100));

    bool hasEth = false;
    bool hasIPv4 = false;
    bool hasUdp = false;
    bool hasDhcp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
            hasEth = true;
        else if (pkt.headers[i].type == HeaderType::IPV4)
            hasIPv4 = true;
        else if (pkt.headers[i].type == HeaderType::UDP)
            hasUdp = true;
        else if (pkt.headers[i].type == HeaderType::DHCP)
            hasDhcp = true;
    }

    EXPECT_TRUE(hasEth);
    EXPECT_TRUE(hasIPv4);
    EXPECT_TRUE(hasUdp);
    EXPECT_FALSE(hasDhcp);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv6 + UDP + DHCPv6 (L5 in your code)
//--------------------------------------------------------------------------------

// Test EthernetIPv6UdpDhcpv6_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv6Dhcpv6_Valid)
{
    std::cout << "DHCPV6 IS NEEDED" << std::endl;
    GTEST_SKIP();
}

// Test EthernetIPv6UdpDhcpv6_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv6Dhcpv6_Invalid)
{
    std::cout << "DHCPV6 IS NEEDED" << std::endl;
    GTEST_SKIP();
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv4 + EIGRP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Eigrp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Eigrp_Valid)
{
    // Ethernet(14) + IPv4(20, proto=88 => EIGRP) + EIGRP(20+)

    // Ethernet
    alignas(64) uint8_t packet[14 + 20 + 20] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        // IPv4 => proto=88(EIGRP)
        0x45, 0x00, 0x00, 0x28, 0x00, 0x00, 0x40, 0x00, 0x11, 0x58, 0x00, 0x00,
        0xC0, 0xA8, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x02,
        // Eigrp
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    };

    EXPECT_TRUE(inspection(pkt, packet, 14 + 20 + 20));

    bool hasEth = false;
    bool hasIPv4 = false;
    bool hasEigrp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
        {
            if (hasEth) FAIL();
            hasEth = true;
        }
        else if (pkt.headers[i].type == HeaderType::IPV4)
        {
            if (hasIPv4) FAIL();
            hasIPv4 = true;
        }
        else if (pkt.headers[i].type == HeaderType::EIGRP)
        {
            if (hasEigrp) FAIL();
            hasEigrp = true;
        }
        else
            FAIL();
    }

    EXPECT_TRUE(hasEth);
    EXPECT_TRUE(hasIPv4);
    EXPECT_TRUE(hasEigrp);
}

// Test EthernetIPv4Eigrp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Eigrp_Invalid)
{
    // Provide only 8 bytes for EIGRP => incomplete
    alignas(64) uint8_t packet[14 + 20 + 8] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        // IPv4 => proto=88(EIGRP)
        0x45, 0x00, 0x00, 0x28, 0x00, 0x00, 0x40, 0x00, 0x11, 0x58, 0x00, 0x00,
        0xC0, 0xA8, 0x01, 0x01, 0xC0, 0xA8, 0x01, 0x02,
        // Eigrp
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55
    };

    EXPECT_FALSE(inspection(pkt, packet, 14 + 20 + 8));

    bool hasEth = false;
    bool hasIPv4 = false;
    bool hasEigrp = false;
    for (int i = 0; i < pkt.count; ++i)
    {
        if (pkt.headers[i].type == HeaderType::ETHERNET)
            hasEth = true;
        else if (pkt.headers[i].type == HeaderType::IPV4)
            hasIPv4 = true;
        else if (pkt.headers[i].type == HeaderType::EIGRP)
            hasEigrp = true;
    }

    EXPECT_TRUE(hasEth);
    EXPECT_TRUE(hasIPv4);
    EXPECT_FALSE(hasEigrp);
}

//--------------------------------------------------------------------------------
// Done
//--------------------------------------------------------------------------------
