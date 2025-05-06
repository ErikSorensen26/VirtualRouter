#include <gtest/gtest.h>
#include "Decapsulation.h"      // Your Packet class
#include "PacketStructure.h"    // All your protocol structs

class TestablePacket : public Packet
{
public:
    // A "do-nothing" constructor: does NOT call inspection(...) automatically
    TestablePacket(ByteString pak = "")
        : Packet(pak)
    {
        print = false;
        start = 0;
        packetInfo = {};
    }

    using Packet::inspection;   // Expose the inspection function for tests
    using Packet::packetInfo;   // Expose the parsed info
    using Packet::start;        // Expose the offset
    using Packet::fullPacket;   // Expose full packet if needed

    // Helper to load the entire "whole packet" into fullPacket
    void setFullPacket(const ByteString &data)
    {
        fullPacket = data;
        start = 0;  // Reset the offset
    }
};

//--------------------------------------------------------------------------------
// Test Fixture
//--------------------------------------------------------------------------------

class Internal_DecapsulationTest : public ::testing::Test 
{
protected:
    TestablePacket* pkt; // Each test has a fresh Packet instance

    // Helper: build ByteString of length `n` filled with byte `fill`
    ByteString createFilled(size_t n, unsigned char fill)
    {
        ByteString bs;
        bs.reserve(n);
        for (size_t i = 0; i < n; i++) {
            bs.push_back(fill);
        }
        return bs;
    }

    void SetUp() override
    {
        pkt = new TestablePacket();
    }

    void TearDown() override
    {
        delete pkt;
    }

    bool inspection(ByteString& packet)
    {
        pkt->fullPacket = packet;
        if (pkt->inspection(packet) && pkt->decapsulate())
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
    ByteString packet = std::string(
       // Ethernet: 6 dst + 6 src + 2 type=0x0806(ARP)
       "\xFF\xFF\xFF\xFF\xFF\xFF"  // dst mac
       "\x11\x11\x22\x33\x44\x55"  // src mac
       "\x08\x06"                  // EtherType = ARP
       // ARP (28 bytes):
       "\x00\x01\x08\x00\x06\x04\x00\x01"  // htype=1, ptype=0x0800, hlen=6, plen=4, opcode=1
       "\xAA\xBB\xCC\xDD\xEE\xFF"          // Sender MAC
       "\xC0\xA8\x01\x01"                  // Sender IP
       "\x11\x22\x33\x44\x55\x66"          // Target MAC
       "\xC0\xA8\x01\x02",                 // Target IP
       42
    );

    bool result = inspection(packet); // Decapsulate everything

    // Check L2: Ethernet
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<EthernetHeader>(pkt->packetInfo.Layer2[0]));

    // Check L2.5: ARP
    ASSERT_EQ(pkt->packetInfo.Layer2_5.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<ArpHeader>(pkt->packetInfo.Layer2_5[0]));
    EXPECT_TRUE(result);
}

// Test EthernetArp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetArp_Invalid)
{
    // Make it too short to contain ARP (only 20 bytes total)
    // Ethernet alone needs 14 bytes, so ARP can't fit
    ByteString packet = std::string(
        // Ethernet: 6 dst + 6 src + 2 type=0x0806(ARP)
        "\xFF\xFF\xFF\xFF\xFF\xFF"  // dst mac
        "\x11\x11\x22\x33\x44\x55"  // src mac
        "\x08\x06"                  // EtherType = ARP
        // Invlaid Arp
        "\xFF\xFF\xFF\xFF",
        18
    );

    bool result = inspection(packet);

    // Likely we can parse Ethernet (because we do have 14 bytes),
    // but there's not enough for ARP (28 needed).
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    EXPECT_TRUE(pkt->packetInfo.Layer2_5.empty()); 
    EXPECT_FALSE(result);
}

//--------------------------------------------------------------------------------
// ETHERNET + MPLS + IPv4
//   (Pretend minimal MPLS label, then an IPv4 header. Real networks might do more.)
//--------------------------------------------------------------------------------

// Test EthernetMplsIPv4_Valid
TEST_F(Internal_DecapsulationTest, EthernetMplsIPv4_Valid)
{
    // Ethernet(14) + MPLS(4) + minimal IPv4(20) = 38 bytes
    ByteString packet = std::string(
       // Ethernet
       "\xFF\xFF\xFF\xFF\xFF\xFF"
       "\x11\x11\x22\x33\x44\x55"
       "\x88\x47"   // EtherType for MPLS unicast
       // MPLS (4 bytes)
       "\x00\x01\x10\xFF" // Label=0x0001, EXP=0, S=1, TTL=255
       // Minimal IPv4 (20 bytes)
       "\x45\x00\x00\x14\x00\x00\x40\x00\x40\x00\x00\x00"
       "\xC0\xA8\x01\x01\xC0\xA8\x01\x02",
       38
    );

    bool result = inspection(packet);

    // L2: Ethernet
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<EthernetHeader>(pkt->packetInfo.Layer2[0]));

    // L2.5: MPLS
    ASSERT_EQ(pkt->packetInfo.Layer2_5.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<MplsHeader>(pkt->packetInfo.Layer2_5[0]));

    // L3: IPv4
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<IPv4Header>(pkt->packetInfo.Layer3[0]));

    EXPECT_TRUE(result);
}

// Test EthernetMplsIPv4_Invalid
TEST_F(Internal_DecapsulationTest, EthernetMplsIPv4_Invalid)
{
    // We'll have Ethernet(14) + MPLS(4) but only 10 bytes for IPv4 => insufficient
    ByteString packet = std::string(
        "\xFF\xFF\xFF\xFF\xFF\xFF"
        "\x00\x11\x22\x33\x44\x55"
        "\x88\x47"
        "\x00\x01\x10\xFF" // MPLS
        // Only 10 bytes for supposed IPv4 => incomplete
        "\x45\x00\x00\x14\x00\x00\x40\x00\x40\x06",
        14 + 4 + 10
    );

    bool result = inspection(packet);

    // L2 ok, L2.5 ok, L3 partial => no actual IPv4 decode
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer2_5.size(), 1u);
    EXPECT_TRUE(pkt->packetInfo.Layer3.empty()); 

    EXPECT_FALSE(result);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv4 + ICMP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Icmp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Icmp_Valid)
{
    // Ethernet(14) + IPv4(20) + ICMP(8) = 42
    ByteString packet = std::string(
       // Ethernet
       "\xFF\xFF\xFF\xFF\xFF\xFF"
       "\x00\x11\x22\x33\x44\x55"
       "\x08\x00"    // EtherType=IPv4
       // IPv4 (20 bytes)
       "\x45\x00\x00\x14\x00\x00\x40\x01\xAA\x01\x12\x34"
       "\xC0\xA8\x01\x01\xC0\xA8\x01\x02"
       // ICMP (8 bytes)
       "\x08\x00\x12\x34\x00\x01\x00\x02",
       14 + 20 + 8
    );

    bool result = inspection(packet);

    // L2 => Ethernet
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    // L3 => IPv4
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 2u);
    // Because code might store IPv4 & ICMP both in Layer3 
    // (some designs treat ICMP as L3). Check the last entry is ICMP
    EXPECT_TRUE(std::holds_alternative<IPv4Header>(pkt->packetInfo.Layer3[0]));
    EXPECT_TRUE(std::holds_alternative<IcmpHeader>(pkt->packetInfo.Layer3[1]));

    EXPECT_TRUE(result);
}

// Test EthernetIPv4Icmp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Icmp_Invalid)
{
    // We'll do Ethernet(14) + IPv4(20) but only 4 bytes left for ICMP => incomplete
    ByteString packet = std::string(
       "\xFF\xFF\xFF\xFF\xFF\xFF"
       "\x00\x11\x22\x33\x44\x55"
       "\x08\x00"
       "\x45\x00\x00\x14\x00\x00\x40\x01\xAA\x01\x12\x34"
       "\xC0\xA8\x01\x01\xC0\xA8\x01\x02"
       // 4 bytes leftover => incomplete ICMP
       "\x08\x00\x12\x34",
       14 + 20 + 4
    );

    bool result = inspection(packet);

    // IPv4 is present, but ICMP decode fails
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<IPv4Header>(pkt->packetInfo.Layer3[0]));
    EXPECT_FALSE(result);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv6 + ICMPv6
//--------------------------------------------------------------------------------

// Test EthernetIPv6Icmpv6_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv6Icmpv6_Valid)
{
    // Ethernet(14) + IPv6(40) + ICMPv6(8) = 62
    ByteString packet = std::string(
       // Ethernet
       "\xFF\xFF\xFF\xFF\xFF\xFF"
       "\x00\x11\x22\x33\x44\x55"
       "\x86\xDD" // EtherType for IPv6
       // Minimal IPv6 (40 bytes, NextHeader=58 => ICMPv6)
       "\x60\x00\x00\x00\x00\x08\x3A\x40"  
       "\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"
       "\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02"
       // ICMPv6(8 bytes)
       "\x80\x00\x12\x34\x00\x00\x00\x00",
       14 + 40 + 8
    );

    bool result = inspection(packet);

    // L2 => Ethernet
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    // L3 => IPv6 + ICMPv6 might be stored. 
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<IPv6Header>(pkt->packetInfo.Layer3[0]));
    EXPECT_TRUE(std::holds_alternative<IcmpV6Header>(pkt->packetInfo.Layer3[1]));
    EXPECT_TRUE(result);
}

// Test EthernetIPv6Icmpv6_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv6Icmpv6_Invalid)
{
    // Just remove some bytes from ICMPv6 => incomplete
    ByteString packet = std::string(
       "\xFF\xFF\xFF\xFF\xFF\xFF"
       "\x00\x11\x22\x33\x44\x55"
       "\x86\xDD"
       "\x60\x00\x00\x00\x00\x08\x3A\x40"  
       "\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"
       "\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02"
       // Only 4 bytes for ICMPv6
       "\x80\x00\x12\x34",
       14 + 40 + 4
    );

    bool result = inspection(packet);

    // We get Ethernet + IPv6
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<IPv6Header>(pkt->packetInfo.Layer3[0]));
    EXPECT_FALSE(result);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv4 + IGMP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Igmp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Igmp_Valid)
{
    // Ethernet(14) + IPv4(20, proto=2 => IGMP) + IGMP(8)
    ByteString packet = std::string(
       "\xFF\xFF\xFF\xFF\xFF\xFF"
       "\x00\x11\x22\x33\x44\x55"
       "\x08\x00"  // IPv4
       // IPv4 => proto=2 (IGMP), length=20
       "\x45\x00\x00\x14\x00\x00\x40\x00\x01\x02\x00\x00"
       "\xC0\xA8\x01\x01\xC0\xA8\x01\x02"
       // IGMP(8 bytes) => type=0x11, ...
       "\x11\x64\x12\x34\xE0\x00\x00\x01",
       14 + 20 + 8
    );

    bool result = inspection(packet);

    // L2 => Ethernet
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    // L3 => IPv4 + IGMP
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<IPv4Header>(pkt->packetInfo.Layer3[0]));
    EXPECT_TRUE(std::holds_alternative<IgmpHeader>(pkt->packetInfo.Layer3[1]));
    EXPECT_TRUE(result);
}

// Test EthernetIPv4Igmp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Igmp_Invalid)
{
    // Truncate IGMP => only 4 bytes
    ByteString packet = std::string(
       "\xFF\xFF\xFF\xFF\xFF\xFF"
       "\x00\x11\x22\x33\x44\x55"
       "\x08\x00"
       "\x45\x00\x00\x14\x00\x00\x40\x00\x01\x02\x00\x00"
       "\xC0\xA8\x01\x01\xC0\xA8\x01\x02"
       "\x11\x64\x12\x34", // only 4 bytes for IGMP
       14 + 20 + 4
    );

    bool result = inspection(packet);

    // IPv4 present, IGMP incomplete
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    EXPECT_FALSE(result);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv4 + TCP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Tcp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Tcp_Valid)
{
    // Ethernet(14) + IPv4(20, proto=6) + TCP(20) => 54
    ByteString packet = std::string(
      "\xFF\xFF\xFF\xFF\xFF\xFF"
      "\x00\x11\x22\x33\x44\x55"
      "\x08\x00"
      // IPv4 => proto=6 (TCP)
      "\x45\x00\x00\x14\x00\x00\x40\x00\x11\x06\x00\x00"
      "\xC0\xA8\x01\x01\xC0\xA8\x01\x02"
      // TCP => 20 bytes
      "\x1F\x90\x00\x50\x00\x00\x00\x00\x00\x00\x00\x00"
      "\x50\x02\x71\x10\x12\x34\x00\x00",
      14 + 20 + 20
    );

    bool result = inspection(packet);

    // L2 => Ethernet
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    // L3 => IPv4
    // L4 => TCP
    // By design, Packet::l3 might store IPv4, then Packet::l4 might store TCP
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer4.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<IPv4Header>(pkt->packetInfo.Layer3[0]));
    EXPECT_TRUE(std::holds_alternative<TcpHeader>(pkt->packetInfo.Layer4[0]));
    EXPECT_TRUE(result);
}

// Test EthernetIPv4Tcp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Tcp_Invalid)
{
    // Truncate TCP => only 10 bytes
    ByteString packet = std::string(
      "\xFF\xFF\xFF\xFF\xFF\xFF"
      "\x00\x11\x22\x33\x44\x55"
      "\x08\x00"
      "\x45\x00\x00\x14\x00\x00\x40\x00\x11\x06\x00\x00"
      "\xC0\xA8\x01\x01\xC0\xA8\x01\x02"
      "\x1F\x90\x00\x50\x00\x00\x00\x00\x50\x02",
      14 + 20 + 10
    );

    bool result = inspection(packet);

    // IPv4 is present, TCP is incomplete
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    EXPECT_TRUE(pkt->packetInfo.Layer4.empty());
    EXPECT_FALSE(result);
}

//--------------------------------------------------------------------------------
// ETHERNET + IPv4 + UDP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Udp_Valid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Udp_Valid)
{
    // Ethernet(14) + IPv4(20, proto=17 => UDP) + UDP(8)
    ByteString packet = std::string(
      "\xFF\xFF\xFF\xFF\xFF\xFF"
      "\x00\x11\x22\x33\x44\x55"
      "\x08\x00"
      // IPv4 => proto=17(UDP)
      "\x45\x00\x00\x14\x00\x00\x40\x00\x11\x11\x00\x00"
      "\xC0\xA8\x01\x01\xC0\xA8\x01\x02"
      // UDP(8)
      "\x1F\x90\x00\x35\x00\x08\x12\x34",
      14 + 20 + 8
    );

    bool result = inspection(packet);

    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer4.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<IPv4Header>(pkt->packetInfo.Layer3[0]));
    EXPECT_TRUE(std::holds_alternative<UdpHeader>(pkt->packetInfo.Layer4[0]));
    EXPECT_TRUE(result);
}

// Test EthernetIPv4Udp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Udp_Invalid)
{
    // Truncated UDP => 4 bytes
    ByteString packet = std::string(
      "\xFF\xFF\xFF\xFF\xFF\xFF"
      "\x00\x11\x22\x33\x44\x55"
      "\x08\x00"
      "\x45\x00\x00\x14\x00\x00\x40\x00\x11\x11\x00\x00"
      "\xC0\xA8\x01\x01\xC0\xA8\x01\x02"
      "\x1F\x90\x00\x35",
      14 + 20 + 4
    );

    bool result = inspection(packet);

    // L4 incomplete
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    EXPECT_TRUE(pkt->packetInfo.Layer4.empty());
    EXPECT_FALSE(result);
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
    const size_t DHCP_SIZE = 236;  // minimal BOOTP + magic cookie
    ByteString dhcpData = std::string(DHCP_SIZE, 0x00) + std::string("\x63\x82\x52\x63\x35\x01\x01\x3D\x07\x01\x00\x1A\x2B\x3C\x4D\x5E\xff", 17);

    ByteString packet = std::string(
      "\xFF\xFF\xFF\xFF\xFF\xFF"
      "\x00\x11\x22\x33\x44\x55"
      "\x08\x00"
      // IPv4 => proto=17(UDP)
      "\x45\x00\x00\x14\x00\x00\x40\x00\x11\x11\x00\x00"
      "\xC0\xA8\x01\x01\xC0\xA8\x01\x02"
      // UDP(8)
      "\x00\x43\x00\x44\x00\x08\x12\x34",
      14 + 20 + 8
    );
    // DHCP => 240 bytes
    packet.append(dhcpData);

    bool result = inspection(packet);

    // L2 => Ethernet
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    // L3 => IPv4
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    // L4 => UDP
    ASSERT_EQ(pkt->packetInfo.Layer4.size(), 1u);
    // L5 => DHCP
    ASSERT_EQ(pkt->packetInfo.Layer5.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<DhcpHeader>(pkt->packetInfo.Layer5[0]));
    EXPECT_TRUE(result);
}

// Test EthernetIPv4UdpDhcp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4UdpDhcp_Invalid)
{
    // Not enough data for minimal DHCP => only 100 bytes
    const size_t DHCP_SIZE = 100;
    ByteString shortDhcp = createFilled(DHCP_SIZE, 0x11);

    ByteString packet = std::string(
      "\xFF\xFF\xFF\xFF\xFF\xFF"
      "\x00\x11\x22\x33\x44\x55"
      "\x08\x00"
      // IPv4 => proto=17(UDP)
      "\x45\x00\x00\x14\x00\x00\x40\x00\x11\x11\x00\x00"
      "\xC0\xA8\x01\x01\xC0\xA8\x01\x02"
      // UDP(8)
      "\x00\x43\x00\x44\x00\x08\x12\x34",
      14 + 20 + 8
    );
    // DHCP => 240 bytes
    packet.append(shortDhcp);

    bool result = inspection(packet);

    // DHCP parse fails
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer4.size(), 1u);
    EXPECT_TRUE(pkt->packetInfo.Layer5.empty());
    EXPECT_FALSE(result);
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
    ByteString eigrpData = createFilled(20, 0x01); // minimal EIGRP content

    // Ethernet
    ByteString packet = std::string("\xFF\xFF\xFF\xFF\xFF\xFF"
      "\x00\x11\x22\x33\x44\x55"
      "\x08\x00"
      // IPv4 => proto=88(EIGRP)
      "\x45\x00\x00\x28\x00\x00\x40\x00\x11\x58\x00\x00"
      "\xC0\xA8\x01\x01\xC0\xA8\x01\x02",
      14 + 20
    );
      // EIGRP(20 bytes)
    packet.append(eigrpData);

    bool result = inspection(packet);

    // L3 => IPv4 + EIGRP
    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer4.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<IPv4Header>(pkt->packetInfo.Layer3[0]));
    EXPECT_TRUE(std::holds_alternative<EigrpHeader>(pkt->packetInfo.Layer4[0]));
    EXPECT_TRUE(result);
}

// Test EthernetIPv4Eigrp_Invalid
TEST_F(Internal_DecapsulationTest, EthernetIPv4Eigrp_Invalid)
{
    // Provide only 8 bytes for EIGRP => incomplete
    ByteString shortEigrp = createFilled(8, 0x55);

    ByteString packet = std::string("\xFF\xFF\xFF\xFF\xFF\xFF"
      "\x00\x11\x22\x33\x44\x55"
      "\x08\x00"
      // IPv4 => proto=88(EIGRP)
      "\x45\x00\x00\x28\x00\x00\x40\x00\x11\x58\x00\x00"
      "\xC0\xA8\x01\x01\xC0\xA8\x01\x02",
      14 + 20
    );
      // EIGRP(20 bytes)
    packet.append(shortEigrp);

    bool result = inspection(packet);

    ASSERT_EQ(pkt->packetInfo.Layer2.size(), 1u);
    ASSERT_EQ(pkt->packetInfo.Layer3.size(), 1u); 
    ASSERT_EQ(pkt->packetInfo.Layer4.size(), 0u);
    EXPECT_FALSE(result);
}

//--------------------------------------------------------------------------------
// Done
//--------------------------------------------------------------------------------
