// Internal_EncapsulationTest.cpp

#include <gtest/gtest.h>
#include <Encapsulation.h>         // Your encapsulate function
#include <PacketStructure.h>      // All your protocol structs
#include <PacketBuilder.hpp>

//--------------------------------------------------------------------------------
// Test Fixture
//--------------------------------------------------------------------------------
class Internal_EncapsulationTest : public ::testing::Test 
{
private:
    uint8_t buffer[2048] = {0};
    PacketSlot* pktslot = nullptr;
    FrameHandle* fhdlr = nullptr;

protected:
    PacketBuilder* pkt = nullptr;
    BuildEntry* h;

    void SetUp() override
    {
        pktslot = new PacketSlot();
        fhdlr = new FrameHandle();
        fhdlr->slot = pktslot;
        fhdlr->payload = buffer;
        fhdlr->qid = 0;

        pkt = new PacketBuilder(*fhdlr);
    }

    void TearDown() override
    {
        delete pkt;
        pkt = nullptr;
        delete fhdlr;
        fhdlr = nullptr;
        delete pktslot;
        pktslot = nullptr;
        std::memset(buffer, 0, 2048);
    }
    
    bool encapsulateThis() { return encapsulate(*pkt); }
};

//--------------------------------------------------------------------------------
// Test Cases
//--------------------------------------------------------------------------------

//--------------------------------------------------------------------------------
// Ethernet Only
//--------------------------------------------------------------------------------


// Test EthernetOnly
TEST_F(Internal_EncapsulationTest, EthernetOnly)
{
    // Build a minimal Ethernet "whole packet":
    // [Ethernet(14)] + [Payload]

    uint8_t ethernetHeader[14] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination Mac (Broadcast)
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
        0x08, 0x00
    };
    
    h = pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    EthernetHeader eth;
    eth.setBuffer(h->buffer);

    // No layer 2.5 or higher headers
    eth.setDestinationMac(ethernetHeader);
    eth.setSourceMac(ethernetHeader + 6);
    eth.setType(ETHERNET_IPV4);

    EXPECT_TRUE(encapsulateThis());
    EXPECT_EQ(std::memcmp(pkt->getBuffer(), ethernetHeader, 14), 0);
}

//--------------------------------------------------------------------------------
// Ethernet + ARP
//--------------------------------------------------------------------------------

// Test EthernetArp
TEST_F(Internal_EncapsulationTest, EthernetArp)
{
    // Build a minimal Ethernet + ARP "whole packet":
    // [Ethernet(14)] + [ARP(28)] = 42 bytes
    uint8_t expectedEthernet[14] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination MAC
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, // Source MAC
        0x08, 0x06
    };

    uint8_t expectedArp[28] = {
        0x00, 0x01, // Hardware Type = Ethernet (1)
        0x08, 0x00, // Protocol Type = IPv4 (0x0800)
        0x06,       // Hardware Size = 6
        0x04,       // Protocol Size = 4
        0x00, 0x01, // Opcode = Request (1)
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // Sender MAC
        0xC0, 0xA8, 0x01, 0x01,             // Sender IP
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, // Target MAC
        0xC0, 0xA8, 0x01, 0x02              // Target IP
    };

    // Reserve space for all headers
    pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    pkt->reserveHeader(HeaderType::ARP, ArpHeader::fixedSize);

    // Build headers
    h = pkt->nextBuildHeader();
    ArpHeader arp;
    arp.setBuffer(h->buffer);
    arp.setHardwareType(ARP_HARDWARE_ETHERNET);
    arp.setHardwareSize(6);
    arp.setProtocolType(ETHERNET_IPV4);
    arp.setProtocolSize(4);
    arp.setOpcode(ARP_OPCODE_REQUEST);
    arp.setSenderHwAddr(expectedArp + 8);
    arp.setSenderIpAddr(expectedArp + 14);
    arp.setTargetHwAddr(expectedArp + 18);
    arp.setTargetIpAddr(expectedArp + 24);

    h = pkt->nextBuildHeader();
    EthernetHeader eth;
    eth.setBuffer(h->buffer);
    eth.setDestinationMac(expectedEthernet);
    eth.setSourceMac(expectedEthernet + 6);
    eth.setType(ETHERNET_ARP);

    EXPECT_TRUE(encapsulateThis());
    EXPECT_EQ(std::memcmp(pkt->getBuffer(), expectedEthernet, 14), 0);
    std::string str = std::string(reinterpret_cast<char*>(pkt->getBuffer() + 14), 28);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 14, expectedArp, 28), 0);
}

//--------------------------------------------------------------------------------
// Ethernet + MPLS + IPv4
//--------------------------------------------------------------------------------

// Test EthernetMplsIPv4
TEST_F(Internal_EncapsulationTest, EthernetMplsIPv4)
{
    // Ethernet(14) + MPLS(4) + IPv4(20) = 38 bytes
    uint8_t expectedEthernet[14] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination MAC
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // Source MAC
        0x88, 0x47 // EtherType = MPLS Unicast
    };

    uint8_t expectedMpls[4] = {
        0x00, 0x01, 0x11, 0xFF // Label=17, EXP=0, S=1, TTL=255
    };

    uint8_t expectedIPv4[20] = {
        0x45, 0x00, // Version/IHL, TOS
        0x00, 0x14, // Total Length = 20
        0x00, 0x01, // Identification
        0x40, 0x00, // Flags, Fragment offset
        0x40, 0x06, // TTL=64, Protocol=TCP(6)
        0xB7, 0x8C, // Header Checksum
        0xC0, 0xAB, 0x01, 0x01, // Source IP: 192.168.1.1
        0xC0, 0xA8, 0x01, 0x02 // Dest IP: 192.168.1.2
    };

    // Reserve all headers
    pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    pkt->reserveHeader(HeaderType::MPLS, MplsHeader::fixedSize);
    pkt->reserveHeader(HeaderType::IPV4, IPv4Header::fixedSize);

    // Build headers
    h = pkt->nextBuildHeader(); // IPv4
    IPv4Header ipv4;
    ipv4.setBuffer(h->buffer);
    ipv4.setVersion(4);
    ipv4.setHeaderLength(5);
    ipv4.setTypeOfService(0);
    ipv4.setIdentification(1);
    ipv4.setFlags(false, false, true); // Don't fragment
    ipv4.setTtl(64);
    ipv4.setProtocol(IP_TCP);
    ipv4.setHeaderChecksum(0x8795);
    ipv4.setSourceAddress(expectedIPv4 + 12);
    ipv4.setDestinationAddress(expectedIPv4 + 16);

    h = pkt->nextBuildHeader(); // MPLS
    MplsHeader mpls;
    mpls.setBuffer(h->buffer);
    mpls.setLabel(17);
    mpls.setExp(0);
    mpls.setBottomOfStack(1);
    mpls.setTtl(255);

    h = pkt->nextBuildHeader(); // Ethernet
    EthernetHeader eth;
    eth.setBuffer(h->buffer);
    eth.setDestinationMac(expectedEthernet);
    eth.setSourceMac(expectedEthernet + 6);
    eth.setType(ETHERNET_MPLS);

    EXPECT_TRUE(encapsulateThis());

    EXPECT_EQ(std::memcmp(pkt->getBuffer(), expectedEthernet, 14), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 14, expectedMpls, 4), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 18, expectedIPv4, 20), 0);
}

//--------------------------------------------------------------------------------
// Ethernet + IPv4 + ICMP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Icmp
TEST_F(Internal_EncapsulationTest, EthernetIPv4Icmp)
{
    GTEST_SKIP() << "Finish implementation for icmpv4";
    // Ethernet(14) + IPv4(20) + ICMP(8) = 42 bytes
    uint8_t expectedEthernet[14] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination MAC
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, // Source MAC
        0x08, 0x00 // EtherType = IPv4
    };

    uint8_t expectedIPv4[20] = {
        0x45, 0x00, // Version/IHL, TOS
        0x00, 0xC1, // Total Length = 28
        0x00, 0x01, // Identification
        0x40, 0x00, // Flags, Fragment Offset
        0x40, 0x01, // TTL=64, Protocol=ICMP
        0xB7, 0x7C, // Header Checksum
        0xC0, 0xA8, 0x01, 0x09, // Source IP: 192.168.1.9
        0xC0, 0xA8, 0x01, 0x0A // Dest IP: 192.168.1.10
    };

    uint8_t expectedIcmp[8] = {
        0x08, 0x00, // Type=8 (Echo Request), Code=0
        0xF7, 0xFC, // Header Checksum
        0x00, 0x01, // Identifier
        0x00, 0x02 // Sequence Number
    };

    // Reserve Headers
    pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    pkt->reserveHeader(HeaderType::IPV4, IPv4Header::fixedSize);
    pkt->reserveHeader(HeaderType::ICMP, IcmpHeader::fixedSize);

    // Build headers
    h = pkt->nextBuildHeader();
    IcmpHeader icmp;
    icmp.setBuffer(h->buffer);
    icmp.setType(0x08);
    icmp.setCode(0);
    icmp.setIdentifier(0x0001);
    icmp.setSequenceNumber(2);

    h = pkt->nextBuildHeader();
    IPv4Header ipv4;
    ipv4.setBuffer(h->buffer);
    ipv4.setVersion(4);
    ipv4.setHeaderLength(5);
    ipv4.setTypeOfService(0);
    ipv4.setIdentification(0x0001);
    ipv4.setFlags(false, false, true);
    ipv4.setTtl(64);
    ipv4.setProtocol(IP_ICMPV4);
    ipv4.setHeaderChecksum(0);
    ipv4.setSourceAddress(expectedIPv4 + 12);
    ipv4.setDestinationAddress(expectedIPv4 + 16);

    h = pkt->nextBuildHeader();
    EthernetHeader eth;
    eth.setBuffer(h->buffer);
    eth.setDestinationMac(expectedEthernet);
    eth.setSourceMac(expectedEthernet + 6);
    eth.setType(ETHERNET_IPV4);

    EXPECT_TRUE(encapsulateThis());

    EXPECT_EQ(std::memcmp(pkt->getBuffer(), expectedEthernet, 14), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 14, expectedIPv4, 20), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 34, expectedIcmp, 8), 0);
}

//--------------------------------------------------------------------------------
// 6. Ethernet + IPv6 + ICMPv6
//--------------------------------------------------------------------------------

// Test EthernetIPv6Icmpv6
TEST_F(Internal_EncapsulationTest, EthernetIPv6Icmpv6)
{
    // Ethernet(14) + IPv6(40) + ICMPv6(8) = 62 bytes
    uint8_t expectedEthernet[14] = {
        0x33, 0x33, 0x00, 0x00, 0x00, 0x16, // Destination MAC
        0xAC, 0x19, 0x8E, 0x44, 0x55, 0x66, // Source MAC
        0x86, 0xDD // EtherType = IPv6
    };

    uint8_t expectedIPv6[40] = {
        0x60, 0x00, 0x00, 0x00, // Version, Traffic Class, Flow Label
        0x00, 0x1C,             // Payload Length = 28
        0x3A,                   // Next Header = ICMPv6
        0x40,                   // Hop Limit = 64
        0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Source: 2001:db8::1
        0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16  // Dest: FF02::16
    };

    uint8_t expectedIcmpv6[28] = {
        0x8F, 0x00, // Type=128 (Echo Request), Code=0
        0x11, 0x21, // Header Checksum
        0x00, 0x00, 0x00, 0x01, // Reserved
        0x04, 0x00, 0x00, 0x00,
        0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x30, 0xAF // Option data
    };

    // Reserve headers
    pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    pkt->reserveHeader(HeaderType::IPV6, IPv6Header::fixedSize);
    pkt->reserveHeader(HeaderType::ICMPV6, Icmpv6Header::fixedSize + 20);

    // Build headers
    h = pkt->nextBuildHeader();
    Icmpv6Header icmpv6;
    icmpv6.setBuffer(h->buffer);
    icmpv6.setType(0x8F);
    icmpv6.setCode(0);
    icmpv6.setReserved(expectedIcmpv6 + 4);
    icmpv6.setTrail(expectedIcmpv6 + 8, 20);

    h = pkt->nextBuildHeader();
    IPv6Header ipv6;
    ipv6.setBuffer(h->buffer);
    ipv6.setVersionTrafficClassFlow(6, 0, 0);
    ipv6.setPayloadLength(28);
    ipv6.setNextHeader(IP_ICMPV6);
    ipv6.setHopLimit(64);
    ipv6.setSourceAddress(expectedIPv6 + 8);
    ipv6.setDestinationAddress(expectedIPv6 + 24);

    h = pkt->nextBuildHeader();
    EthernetHeader eth;
    eth.setBuffer(h->buffer);
    eth.setDestinationMac(expectedEthernet);
    eth.setSourceMac(expectedEthernet + 6);
    eth.setType(ETHERNET_IPV6);

    EXPECT_TRUE(encapsulateThis());

    EXPECT_EQ(std::memcmp(pkt->getBuffer(), expectedEthernet, 14), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 14, expectedIPv6, 40), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 54, expectedIcmpv6, 28), 0);
}

//--------------------------------------------------------------------------------
// 8. Ethernet + IPv4 + TCP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Tcp
TEST_F(Internal_EncapsulationTest, EthernetIPv4Tcp)
{
    // Ethernet(14) + IPv4(20, proto=6 => TCP) + TCP(20) = 54 bytes
    uint8_t expectedEthernet[14] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // Destination MAC
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, // Source MAC
        0x08, 0x00 // EtherType = IPv4
    };

    uint8_t expectedIPv4[20] = {
        0x45, 0x00, // Version/IHL, TOS
        0x00, 0x28, // Total Length
        0x00, 0x01, // Identification
        0x40, 0x00, // Flags, Fragment Offset
        0x40, 0x06, // TTL=64, Protocol=TCP
        0xB7, 0x5B, // Header Checksum
        0xC0, 0xA8, 0x01, 0x11, // Source IP: 192.168.1.17
        0xC0, 0xA8, 0x01, 0x12  // Source IP: 192.168.1.18
    };

    uint8_t expectedTcp[20] = {
        0x00, 0x50, // Source Port = 80
        0x01, 0xBB, // Destination Port = 443
        0x00, 0x00, 0x00, 0x01, // Sequence Number
        0x00, 0x00, 0x00, 0x00, // Ack Number
        0x50, 0x02, // Data Offset=5, Flags=SYN
        0x71, 0x10, // Window Size
        0xB9, 0x52, // Checksum
        0x00, 0x00  // Urgent Pointer
    };

    // Reserve headers
    pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    pkt->reserveHeader(HeaderType::IPV4, IPv4Header::fixedSize);
    pkt->reserveHeader(HeaderType::TCP, TcpHeader::fixedSize);

    // Build Headers
    h = pkt->nextBuildHeader();
    TcpHeader tcp;
    tcp.setBuffer(h->buffer);
    tcp.setSourcePort(80);
    tcp.setDestinationPort(443);
    tcp.setSequenceNumber(1);
    tcp.setAckNumber(0);
    tcp.setHeaderLengthBytes(5);
    tcp.setFlagSYN(true);
    tcp.setWindowSize(0x7110);
    tcp.setUrgentPointer(0);

    h = pkt->nextBuildHeader();
    IPv4Header ipv4;
    ipv4.setBuffer(h->buffer);
    ipv4.setVersion(4);
    ipv4.setHeaderLength(5);
    ipv4.setTypeOfService(0);
    ipv4.setIdentification(1);
    ipv4.setFlags(false, false, true);
    ipv4.setTtl(64);
    ipv4.setProtocol(IP_TCP);
    ipv4.setHeaderChecksum(0);
    ipv4.setSourceAddress(expectedIPv4 + 12);
    ipv4.setDestinationAddress(expectedIPv4 + 16);

    h = pkt->nextBuildHeader();
    EthernetHeader eth;
    eth.setBuffer(h->buffer);
    eth.setDestinationMac(expectedEthernet);
    eth.setSourceMac(expectedEthernet + 6);
    eth.setType(ETHERNET_IPV4);

    EXPECT_TRUE(encapsulateThis());

    EXPECT_EQ(std::memcmp(pkt->getBuffer(), expectedEthernet, 14), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 14, expectedIPv4, 20), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 34, expectedTcp, 20), 0);
}

//--------------------------------------------------------------------------------
// Ethernet + IPv4 + UDP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Udp
TEST_F(Internal_EncapsulationTest, EthernetIPv4Udp)
{
    // Ethernet(14) + IPv4(20, proto=17 => UDP) + UDP(8) = 42 bytes
    uint8_t expectedEthernet[14] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // Destination MAC
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, // Source MAC
        0x08, 0x00 // EtherType = IPv4
    };

    uint8_t expectedIPv4[20] = {
        0x45, 0x00, // Version/IHL, TOS
        0x00, 0x1C, // Total Length
        0x00, 0x0B, // Identification
        0x40, 0x00, // Flags, Fragment Offset
        0x40, 0x11, // TTL=64, Protocol=UDP
        0xB7, 0x4A, // Header Checksum
        0xC0, 0xA8, 0x01, 0x15, // Source IP: 192.168.1.21
        0xC0, 0xA8, 0x01, 0x16  // Dest IP: 192.168.1.22
    };

    uint8_t expectedUdp[8] = {
        0x00, 0x35, // Source Port 53
        0x00, 0x43, // Destination Port 67
        0x00, 0x08, // Length = 8
        0x7B, 0xEA  // Checksum
    };

    // Reserve headers
    pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    pkt->reserveHeader(HeaderType::IPV4, IPv4Header::fixedSize);
    pkt->reserveHeader(HeaderType::UDP, UdpHeader::fixedSize);

    // Build headers
    h = pkt->nextBuildHeader();
    UdpHeader udp;
    udp.setBuffer(h->buffer);
    udp.setSourcePort(53);
    udp.setDestinationPort(67);

    h = pkt->nextBuildHeader();
    IPv4Header ipv4;
    ipv4.setBuffer(h->buffer);
    ipv4.setVersion(4);
    ipv4.setHeaderLength(5);
    ipv4.setTypeOfService(0);
    ipv4.setIdentification(11);
    ipv4.setFlags(false, false, true);
    ipv4.setTtl(64);
    ipv4.setProtocol(IP_UDP);
    ipv4.setSourceAddress(expectedIPv4 + 12);
    ipv4.setDestinationAddress(expectedIPv4 + 16);

    h = pkt->nextBuildHeader();
    EthernetHeader eth;
    eth.setBuffer(h->buffer);
    eth.setDestinationMac(expectedEthernet);
    eth.setSourceMac(expectedEthernet + 6);
    eth.setType(ETHERNET_IPV4);

    EXPECT_TRUE(encapsulateThis());
    
    EXPECT_EQ(std::memcmp(pkt->getBuffer(), expectedEthernet, 14), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 14, expectedIPv4, 20), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 34, expectedUdp, 8), 0);
}

//--------------------------------------------------------------------------------
// Ethernet + IPv4 + UDP + DHCP
//--------------------------------------------------------------------------------

// Test EthernetIPv4UdpDhcp
TEST_F(Internal_EncapsulationTest, EthernetIPv4UdpDhcp)
{
    // Ethernet(14) + IPv4(20, proto=17 => UDP) + UDP(8) + DHCP(248)
    uint8_t expectedEthernet[14] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // Destination MAC
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, // Source MAC
        0x08, 0x00 // EtherType = IPv4
    };

    uint8_t expectedIPv4[20] = {
        0x45, 0x00, // Version
        0x01, 0x14, // Total Length
        0x00, 0x0D, // Identification
        0x40, 0x00, // Flags, Fragment Offset
        0x40, 0x11, // TTL=64, Protocol=UDP
        0x78, 0x08, // Header Checksum
        0xC0, 0xAb, 0x01, 0x19, // Source IP: 192.168.
    };

    uint8_t expectedUdp[8] = {
        0x00, 0x43, // Source Port 67
        0x00, 0x44, // Destination Port 68
        0x01, 0x00, // Length
        0xDD, 0x7B  // Header Checksum
    };

    uint8_t expectedDhcp[248] = {
        0x02, // Message type: Boot Reply
        0x01, // Hardware Type: Ethernet
        0x06, // Hardware Address Length
        0x00, // Hops
        0x39, 0x03, 0xF3, 0x26, // Transaction ID
        0x00, 0x00, // Seconds Elapsed
        0x80, 0x00, // BOOTP flags: Broadcast
        0x00, 0x00, 0x00, 0x00, // Client IP Address
        0xC0, 0xA8, 0x00, 0x64, // Your (client) IP address
        0xC0, 0xA8, 0x00, 0x01, // Next server IP address
        0xC0, 0xA8, 0x00, 0x02, // Relay agent IP address
        0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E, // Client Hardware Address (MAC)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x63, 0x82, 0x53, 0x63, // Magic Cookie
        0x35, 0x01, 0x02, // DHCP Option 53
        0xFF,
        0x00, 0x00, 0x00, 0x00
    };

    // Reserve headers
    pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    pkt->reserveHeader(HeaderType::IPV4, IPv4Header::fixedSize);
    pkt->reserveHeader(HeaderType::UDP, UdpHeader::fixedSize);
    pkt->reserveHeader(HeaderType::DHCP, 248);

    // Build headers
    h = pkt->nextBuildHeader();
    DhcpHeader dhcp;
    dhcp.setBuffer(h->buffer);
    dhcp.setOpcode(DHCP_TYPE_OFFER);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setHops(0);
    dhcp.setXid(0x3903F326);
    dhcp.setSecs(0);
    dhcp.setFlags(expectedDhcp + 10);
    dhcp.setClientIp(expectedDhcp + 12);
    dhcp.setYourIp(expectedDhcp + 16);
    dhcp.setNextServerIP(expectedDhcp + 20);
    dhcp.setRelayAgentIp(expectedDhcp + 24);
    dhcp.setClientMac(expectedDhcp + 28);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    auto trail = dhcp.getTrail();
    TLV8BufferManager opts(trail.data(), 4);
    uint8_t type = DHCP_TYPE_OFFER;
    opts.append(DHCP_OPTION_TYPE, 1, &type, 1);
    opts.addTermination(DHCP_OPTION_END);

    h = pkt->nextBuildHeader();
    UdpHeader udp;
    udp.setBuffer(h->buffer);
    udp.setSourcePort(67);
    udp.setDestinationPort(68);

    h = pkt->nextBuildHeader();
    IPv4Header ipv4;
    ipv4.setBuffer(h->buffer);
    ipv4.setVersion(4);
    ipv4.setHeaderLength(5);
    ipv4.setTypeOfService(0);
    ipv4.setIdentification(13);
    ipv4.setFlags(false, false, true);
    ipv4.setTtl(64);
    ipv4.setProtocol(IP_UDP);
    ipv4.setSourceAddress(expectedIPv4 + 12);
    ipv4.setDestinationAddress(expectedIPv4 + 16);

    h = pkt->nextBuildHeader();
    EthernetHeader eth;
    eth.setBuffer(h->buffer);
    eth.setDestinationMac(expectedEthernet);
    eth.setSourceMac(expectedEthernet + 6);
    eth.setType(ETHERNET_IPV4);

    EXPECT_TRUE(encapsulateThis());

    EXPECT_EQ(std::memcmp(pkt->getBuffer(), expectedEthernet, 14), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 14, expectedIPv4, 20), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 34, expectedUdp, 8), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 42, expectedDhcp, 248), 0);
}

// Test EthernetIPv4Eigrp
TEST_F(Internal_EncapsulationTest, EthernetIPv4Eigrp)
{
    // Build Ethernet Header
    uint8_t expectedEthernet[14] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // Destination MAC
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, // Source MAC
        0x08, 0x00 // EtherType = IPv4
    };

    // Build IPv4 Header
    uint8_t expectedIPv4[20] = {
        0x45, 0x00, // Version/IHL, TOS
        0x00, 0x30, // Total Length = 48
        0x00, 0x0D, // Identification
        0x40, 0x00, // Flags, Fragment Offset
        0x40, 0x58, // TTL=64, Protocol=88 (EIGRP)
        0xB6, 0xD7, // Header Checksum
        0xC0, 0xA8, 0x01, 0x20, // Source IP: 192.168.1.32
        0xC0, 0xA8, 0x01, 0x21  // Dest IP: 192.168.1.33
    };

    // Build EIGRP Header
    uint8_t expectedEigrp[28] = {
        0x01, 0x05, // Version, Opcode (Hello)
        0x5D, 0xED, // Checksum
        0x00, 0x00, 0x00, 0x00, // Flags
        0x00, 0x00, 0x00, 0x01, // Sequence number
        0x00, 0x00, 0x00, 0x02, // Ack number
        0x00, 0x01, 0x00, 0x64, // VRID=1, AS=100
        0x01, 0x04, 0xDE, 0xAD, // TLV 1
        0x02, 0x04, 0xBE, 0xEF  // TLV 2
    };

    // Reserve headers
    pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    pkt->reserveHeader(HeaderType::IPV4, IPv4Header::fixedSize);
    pkt->reserveHeader(HeaderType::EIGRP, sizeof expectedEigrp);

    h = pkt->nextBuildHeader();
    EigrpHeader eigrp;
    eigrp.setBuffer(h->buffer);
    eigrp.setVersion(1);
    eigrp.setOpcode(EIGRP_TYPE_HELLO);
    eigrp.setSequence(1);
    eigrp.setAck(2);
    eigrp.setVirtualRouterId(1);
    eigrp.setAutonomousSystem(100);
    auto trail = eigrp.getTrail();
    TLV8BufferManager opts(trail.data(), 8);
    opts.append(0x01, 4, expectedEigrp + 22, 2);
    opts.append(0x02, 4, expectedEigrp + 26, 2);

    h = pkt->nextBuildHeader();
    IPv4Header ipv4;
    ipv4.setBuffer(h->buffer);
    ipv4.setVersion(4);
    ipv4.setHeaderLength(5);
    ipv4.setTypeOfService(0);
    ipv4.setIdentification(13);
    ipv4.setFlags(false, false, true);
    ipv4.setTtl(64);
    ipv4.setProtocol(IP_EIGRP);
    ipv4.setHeaderChecksum(0xB6D7);
    ipv4.setSourceAddress(expectedIPv4 + 12);
    ipv4.setDestinationAddress(expectedIPv4 + 16);

    h = pkt->nextBuildHeader();
    EthernetHeader eth;
    eth.setBuffer(h->buffer);
    eth.setDestinationMac(expectedEthernet);
    eth.setSourceMac(expectedEthernet + 6);
    eth.setType(ETHERNET_IPV4);

    EXPECT_TRUE(encapsulateThis());
    EXPECT_EQ(std::memcmp(pkt->getBuffer(), expectedEthernet, 14), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 14, expectedIPv4, 20), 0);
    EXPECT_EQ(std::memcmp(pkt->getBuffer() + 34, expectedEigrp, 28), 0);
}
