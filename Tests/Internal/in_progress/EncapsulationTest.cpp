
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
        std::memset(buffer, 0, 2048);
        delete pktslot;
        delete fhdlr;
    }
};

//--------------------------------------------------------------------------------
// Test Cases
//--------------------------------------------------------------------------------

//--------------------------------------------------------------------------------
// Ethernet Only
//--------------------------------------------------------------------------------


// Test EthernetOnly_Valid
TEST_F(Internal_EncapsulationTest, EthernetOnly_Valid)
{
    // Build a minimal Ethernet "whole packet":
    // [Ethernet(14)] + [Payload]

    uint8_t ethernetHeader[14] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination Mac (Broadcast)
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
        0x08, 0x00
    };
    
    pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    h = pkt->nextBuildHeader();
    EthernetHeader eth;
    eth.setBuffer(h->buffer);

    // No layer 2.5 or higher headers
    eth.setDestinationMac(ethernetHeader);
    eth.setSourceMac(ethernetHeader + 6);
    eth.setType(Variable::Ethernet::ipv4);

    EXPECT_TRUE(encapsulate);
    EXPECT_EQ(std::memcmp(pkt->getBuffer(), ethernetHeader, 14), 0);
}

// Test EthernetOnly_Invalid
TEST_F(Internal_EncapsulationTest, EthernetOnly_Invalid)
{
    // No Layer2.5 or higher headers
    pkt->reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize - 2);
    h = pkt->nextBuildHeader();

    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xFF\xFF\xFF\xFF\xFF\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44", 4); // Incomplete
    eth.type = std::string("", 0); // Missing EtherType
    packetInfo.Layer2.push_back(eth);

    ByteString encapsulated = ByteString("PayloadData");

    auto result = encapsulate(packetInfo, encapsulated);

    EXPECT_FALSE(result.has_value());
}

//--------------------------------------------------------------------------------
// Ethernet + ARP
//--------------------------------------------------------------------------------

// Test EthernetArp_Valid
TEST_F(Internal_EncapsulationTest, EthernetArp_Valid)
{
    // Build a minimal Ethernet + ARP "whole packet":
    // [Ethernet(14)] + [ARP(28)] = 42 bytes
    ByteString ethernetHeader = std::string(
        "\xFF\xFF\xFF\xFF\xFF\xFF"  // Destination MAC (Broadcast)
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x06",                 // EtherType = ARP (0x0806)
        14
    );

    ByteString arpHeader = std::string(
        "\x00\x01"                  // Hardware type = Ethernet (1)
        "\x08\x00"                  // Protocol type = IPv4 (0x0800)
        "\x06"                      // Hardware size = 6
        "\x04"                      // Protocol size = 4
        "\x00\x01"                  // Opcode = request (1)
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Sender MAC
        "\xC0\xA8\x01\x01"          // Sender IP
        "\x11\x22\x33\x44\x55\x66"  // Target MAC
        "\xC0\xA8\x01\x02",         // Target IP
        28
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xFF\xFF\xFF\xFF\xFF\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x06", 2); // ARP
    packetInfo.Layer2.push_back(eth);

    ArpHeader arp;
    arp.hardwareType = std::string("\x00\x01", 2); // Ethernet
    arp.protocolType = std::string("\x08\x00", 2); // IPv4
    arp.hardwareSize = std::string("\x06", 1);
    arp.protocolSize = std::string("\x04", 1);
    arp.opcode = std::string("\x00\x01", 2); // Request
    arp.senderHardwareAddress = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    arp.senderIpAddress = std::string("\xC0\xA8\x01\x01", 4);
    arp.targetHardwareAddress = std::string("\x11\x22\x33\x44\x55\x66", 6);
    arp.targetIpAddress = std::string("\xC0\xA8\x01\x02", 4);
    packetInfo.Layer2_5.push_back(arp);

    ByteString encapsulated = ByteString(""); // No payload for ARP

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet string
    ByteString expectedEth = ethernetHeader;

    // Expected ARP string
    ByteString expectedArp = arpHeader;

    // Expected result
    ByteString expected = expectedEth + expectedArp + encapsulated;

    EXPECT_EQ(result.value().toHex(), expected.toHex());
}

// Test EthernetArp_Invalid
TEST_F(Internal_EncapsulationTest, EthernetArp_Invalid)
{
    // Build an Ethernet + ARP packet with incomplete ARP header
    ByteString ethernetHeader = std::string(
        "\xFF\xFF\xFF\xFF\xFF\xFF"  // Destination MAC (Broadcast)
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x06",                 // EtherType = ARP (0x0806)
        14
    );

    ByteString arpHeader = std::string(
        "\x00\x01"                  // Hardware type = Ethernet (1)
        "\x08\x00"                  // Protocol type = IPv4 (0x0800)
        "\x06"                      // Hardware size = 6
        "\x04"                      // Protocol size = 4
        "\x00\x01"                  // Opcode = request (1)
        "\xAA\xBB\xCC\xDD\xEE"      // Incomplete Sender MAC (5 bytes instead of 6)
        "\xC0\xA8\x01\x01"          // Sender IP
        "\x11\x22\x33\x44\x55\x66"  // Target MAC
        "\xC0\xA8\x01\x02",         // Target IP
        27 // One byte short
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xFF\xFF\xFF\xFF\xFF\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x06", 2); // ARP
    packetInfo.Layer2.push_back(eth);

    ArpHeader arp;
    arp.hardwareType = std::string("\x00\x01", 2); // Ethernet
    arp.protocolType = std::string("\x08\x00", 2); // IPv4
    arp.hardwareSize = std::string("\x06", 1);
    arp.protocolSize = std::string("\x04", 1);
    arp.opcode = std::string("\x00\x01", 2); // Request
    arp.senderHardwareAddress = std::string("\xAA\xBB\xCC\xDD\xEE", 5); // Incomplete
    arp.senderIpAddress = std::string("\xC0\xA8\x01\x01", 4);
    arp.targetHardwareAddress = std::string("\x11\x22\x33\x44\x55\x66", 6);
    arp.targetIpAddress = std::string("\xC0\xA8\x01\x02", 4);
    packetInfo.Layer2_5.push_back(arp);

    ByteString encapsulated = ByteString(""); // No payload for ARP

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet string
    ByteString expectedEth = ethernetHeader;

    EXPECT_FALSE(result.has_value());
}

//--------------------------------------------------------------------------------
// Ethernet + MPLS + IPv4
//--------------------------------------------------------------------------------

// Test EthernetMplsIPv4_Valid
TEST_F(Internal_EncapsulationTest, EthernetMplsIPv4_Valid)
{
    // Ethernet(14) + MPLS(4) + IPv4(20) = 38 bytes
    ByteString ethernetHeader = std::string(
        "\xFF\xFF\xFF\xFF\xFF\xFF"  // Destination MAC (Broadcast)
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Source MAC
        "\x88\x47",                 // EtherType = MPLS Unicast (0x8847)
        14
    );

    ByteString mplsHeader = std::string(
        "\x00\x01\x11\xFF",          // Label=1, EXP=0, S=1, TTL=255
        4
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x00\x14"                    // Total Length = 20
        "\x00\x01"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x00"                    // TTL, Protocol=6 (TCP)
        "\xB7\x95"                    // Header checksum
        "\xC0\xA8\x01\x01"            // Source IP
        "\xC0\xA8\x01\x02",           // Destination IP
        20
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xFF\xFF\xFF\xFF\xFF\xFF", 6);
    eth.sourceMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.type = std::string("\x88\x47", 2); // MPLS Unicast
    packetInfo.Layer2.push_back(eth);

    MplsHeader mpls;
    mpls.label = "000111"; // Label=1, EXP=0, S=1
    mpls.TTL = "FF";           // TTL=255
    packetInfo.Layer2_5.push_back(mpls);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x01", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x00", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x01", 4); // 192.168.1.1
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x02", 4); // 192.168.1.2
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(std::move(ipv4));

    ByteString encapsulated = ByteString("");

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet + MPLS + IPv4 + Payload
    ByteString expected = ethernetHeader + mplsHeader + ipv4Header + encapsulated;

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().toHex(), expected.toHex());
}

// Test EthernetMplsIPv4_Invalid
TEST_F(Internal_EncapsulationTest, EthernetMplsIPv4_Invalid)
{
    // Ethernet(14) + MPLS(4) + incomplete IPv4(10) = 28 bytes
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x88\x47",                 // EtherType = MPLS Unicast
        14
    );

    ByteString mplsHeader = std::string(
        "\x00\x02\x10\xFE",          // Label=2, EXP=0, S=1, TTL=254
        4
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x00\x14"                    // Total Length = 20 (but we provide only 10)
        "\x00\x02"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x00",                   // TTL, Protocol=6 (TCP)
        10
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x88\x47", 2); // MPLS Unicast
    packetInfo.Layer2.push_back(eth);

    MplsHeader mpls;
    mpls.label = "000210"; // Label=2, EXP=0, S=1
    mpls.TTL = "FE";          // TTL=254
    packetInfo.Layer2_5.push_back(mpls);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x02", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x00", 1);
    // Incomplete IPv4 header
    packetInfo.Layer3.push_back(ipv4);

    ByteString encapsulated = ByteString("Incomplete TCP");

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet + MPLS + incomplete IPv4 + Payload
    ByteString expected = ethernetHeader + mplsHeader + ipv4Header + encapsulated;

    EXPECT_FALSE(result.has_value());
}

//--------------------------------------------------------------------------------
// Ethernet + IPv4 + ICMP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Icmp_Valid
TEST_F(Internal_EncapsulationTest, EthernetIPv4Icmp_Valid)
{
    // Ethernet(14) + IPv4(20) + ICMP(8) = 42 bytes
    ByteString ethernetHeader = std::string(
        "\xFF\xFF\xFF\xFF\xFF\xFF"  // Destination MAC (Broadcast)
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType = IPv4 (0x0800)
        14
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x00\x1C"                    // Total Length = 20
        "\x00\x01"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x01"                    // TTL=64, Protocol=1 (ICMP)
        "\xB7\x7C"                    // Header checksum
        "\xC0\xA8\x01\x09"            // Source IP
        "\xC0\xA8\x01\x0A",           // Destination IP
        20
    );

    ByteString icmpHeader = std::string(
        "\x08\x00"                    // Type=8 (Echo Request), Code=0
        "\xF7\xFC"                    // Checksum
        "\x00\x01"                    // Identifier
        "\x00\x02",                   // Sequence Number
        8
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xFF\xFF\xFF\xFF\xFF\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x01", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x01", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x09", 4); // 192.168.1.9
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x0A", 4); // 192.168.1.11
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(ipv4);

    IcmpHeader icmp;
    icmp.type = std::string("\x08", 1);         // Echo Request
    icmp.code = std::string("\x00", 1);         // Code=0
    icmp.checksum = std::string("\x12\x34", 2); // Checksum
    icmp.identifier = std::string("\x00\x01", 2); // Identifier
    icmp.sequenceNumber = std::string("\x00\x02", 2); // Sequence Number
    packetInfo.Layer3.push_back(icmp);

    ByteString encapsulated = ByteString(""); // No payload for ICMP

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet + IPv4 + ICMP + Payload
    ByteString expected = ethernetHeader + ipv4Header + icmpHeader + encapsulated;

    EXPECT_EQ(result.value().toHex(), expected.toHex());
}

// Test EthernetIPv4Icmp_Invalid
TEST_F(Internal_EncapsulationTest, EthernetIPv4Icmp_Invalid)
{
    // Ethernet(14) + IPv4(20) + incomplete ICMP(4) = 38 bytes
    ByteString ethernetHeader = std::string(
        "\xAA\xAA\xAA\xAA\xAA\xAA"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType = IPv4 (0x0800)
        14
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x00\x14"                    // Total Length = 20
        "\x00\x06"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x01"                    // TTL=64, Protocol=1 (ICMP)
        "\x00\x00"                    // Header checksum
        "\xC0\xA8\x01\x0B"            // Source IP
        "\xC0\xA8\x01\x0C",           // Destination IP
        20
    );

    ByteString icmpHeader = std::string(
        "\x08\x00"                    // Type=8 (Echo Request), Code=0
        "\x56\x78",                   // Checksum
        4 // Incomplete (should be 8)
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xAA\xAA\xAA\xAA\xAA", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x01", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x00", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x01", 4); // 192.168.1.1
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x02", 4); // 192.168.1.2
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(ipv4);

    IcmpHeader icmp;
    icmp.type = std::string("\x08", 1);         // Echo Request
    icmp.code = std::string("\x00", 1);         // Code=0
    icmp.checksum = std::string("\x56\x78", 2); // Checksum
    packetInfo.Layer3.push_back(icmp);

    ByteString encapsulated = ByteString(""); // No payload for ICMP

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet + IPv4 + incomplete ICMP + Payload
    ByteString expected = ethernetHeader + ipv4Header + icmpHeader + encapsulated;

    EXPECT_FALSE(result.has_value());
}

//--------------------------------------------------------------------------------
// 6. Ethernet + IPv6 + ICMPv6
//--------------------------------------------------------------------------------

// Test EthernetIPv6Icmpv6_Valid
TEST_F(Internal_EncapsulationTest, EthernetIPv6Icmpv6_Valid)
{
    // Ethernet(14) + IPv6(40) + ICMPv6(8) = 62 bytes
    ByteString ethernetHeader = std::string(
        "\x33\x33\x00\x00\x00\x16"  // Destination MAC
        "\xAC\x19\x8E\x44\x55\x66"  // Source MAC
        "\x86\xDD",                 // EtherType = IPv6 (0x86DD)
        14
    );

    ByteString ipv6Header = std::string(
        "\x60\x00\x00\x00\x00\x1C\x3A\x40"  
        "\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"
        "\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x16",
        40
    );

    ByteString icmpv6Header = std::string(
        "\x8F\x00\x11\x21"            // Type=128 (Echo Request), Code=0, Checksum=0x1234
        "\x00\x00\x00\x01"            // Reserved
        "\x04\x00\x00\x00\xFF\x02\x00\x00\00\x00\x00\x00\x00\x00\x00\x01\xFF\x00\x30\xAF", // Option
        28 // Partial (should be at least 8)
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\x33\x33\x00\x00\x00\x16", 6);
    eth.sourceMac = std::string("\xAC\x19\x8E\x44\x55\x66", 6);
    eth.type = std::string("\x86\xDD", 2); // IPv6
    packetInfo.Layer2.push_back(eth);

    IPv6Header ipv6;
    ipv6.version = "6";  // Version=6
    ipv6.trafficClass = "00";
    ipv6.flowLabel = "00000";
    ipv6.payloadLength = std::string("\x00\x08", 2); // Payload Length=8
    ipv6.protocol = std::string("\x3A", 1);         // Next Header=ICMPv6 (58)
    ipv6.hopLimit = std::string("\x40", 1);         // Hop Limit=64
    ipv6.sourceAddress = std::string("\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16); // 2001:db8::1
    ipv6.destinationAddress = std::string("\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x16", 16); // ff02::16
    packetInfo.Layer3.push_back(ipv6);

    IcmpV6Header icmpv6;
    icmpv6.type = std::string("\x8F", 1);           // Echo Request
    icmpv6.code = std::string("\x00", 1);           // Code=0
    icmpv6.checksum = std::string("\x00\x00", 2);   // Checksum
    icmpv6.reserved = std::string("\x00\x00\x00\x01", 4); // Reserved
    IcmpV6Header::Option option;
    option.option = std::string("\x04\x00\x00\x00\xFF\x02\x00\x00\00\x00\x00\x00\x00\x00\x00\x01\xFF\x00\x30\xAF", 20);
    option.length = std::string("", 0);
    option.value = std::string("", 0);
    icmpv6.options.push_back(option);
    packetInfo.Layer3.push_back(icmpv6);

    ByteString encapsulated = ByteString(""); // No payload for ICMPv6

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet + IPv6 + ICMPv6 + Payload
    ByteString expected = ethernetHeader + ipv6Header + icmpv6Header + encapsulated;

    EXPECT_EQ(result.value().toHex(), expected.toHex());
}

// Test EthernetIPv6Icmpv6_Invalid
TEST_F(Internal_EncapsulationTest, EthernetIPv6Icmpv6_Invalid)
{
    // Ethernet(14) + IPv6(40) + incomplete ICMPv6(4) = 58 bytes
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x86\xDD",                 // EtherType = IPv6 (0x86DD)
        14
    );

    ByteString ipv6Header = std::string(
        "\x60\x00\x00\x00\x00\x08\x3A\x40"  
        "\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"
        "\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02",
        40
    );

    ByteString icmpv6Header = std::string(
        "\x80\x00\x12\x34",           // Type=128 (Echo Request), Code=0, Checksum=0x1234
        4 // Incomplete
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x86\xDD", 2); // IPv6
    packetInfo.Layer2.push_back(eth);

    IPv6Header ipv6;
    ipv6.version = "6";  // Version=6
    ipv6.trafficClass = "00";
    ipv6.flowLabel = "00000";
    ipv6.payloadLength = std::string("\x00\x08", 2); // Payload Length=8
    ipv6.protocol = std::string("\x3A", 1);         // Next Header=ICMPv6 (58)
    ipv6.hopLimit = std::string("\x40", 1);         // Hop Limit=64
    ipv6.sourceAddress = std::string("\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16); // 2001:db8::1
    ipv6.destinationAddress = std::string("\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02", 16); // 2001:db8::2
    packetInfo.Layer3.push_back(ipv6);

    IcmpV6Header icmpv6;
    icmpv6.type = std::string("\x80", 1);           // Echo Request
    icmpv6.code = std::string("\x00", 1);           // Code=0
    icmpv6.checksum = std::string("\x12\x34", 2);   // Checksum
    // Incomplete options
    packetInfo.Layer3.push_back(icmpv6);

    ByteString encapsulated = ByteString(""); // No payload for ICMPv6

    auto result = encapsulate(packetInfo, encapsulated);

    EXPECT_FALSE(result.has_value());
}

//--------------------------------------------------------------------------------
// Ethernet + IPv4 + IGMP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Igmp_Valid
TEST_F(Internal_EncapsulationTest, EthernetIPv4Igmp_Valid)
{
    // Ethernet(14) + IPv4(20, proto=2 => IGMP) + IGMP(8) = 42 bytes
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType = IPv4 (0x0800)
        14
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x00\x1C"                    // Total Length = 30
        "\x00\x01"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x02"                    // TTL=64, Protocol=2 (IGMP)
        "\xB7\x73"                    // Header checksum
        "\xC0\xA8\x01\x0D"            // Source IP
        "\xC0\xA8\x01\x0E",           // Destination IP
        20
    );

    ByteString igmpHeader = std::string(
        "\x11\x64"                    // Type=17 (IGMPv3 Membership Report), Max Resp Time=100
        "\xEF\x9A"                    // Checksum
        "\xFF\x00\x00\x00",                   // Multicast Address
        8
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x01", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x02", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x0D", 4); // 192.168.1.14
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x0E", 4); // 192.168.1.15
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(ipv4);

    IgmpHeader igmp;
    igmp.type = std::string("\x11", 1);         // Type=17 (IGMPv3 Membership Report)
    igmp.maxRestTime = std::string("\x64", 1);  // Max Resp Time=100
    igmp.checksum = std::string("\x12\x34", 2); // Checksum
    igmp.multicastAddress = std::string("\xFF\x00\x00\x00", 4); // Multicast Address
    packetInfo.Layer3.push_back(igmp);

    ByteString encapsulated = ByteString(""); // No payload for IGMP

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet + IPv4 + IGMP + Payload
    ByteString expected = ethernetHeader + ipv4Header + igmpHeader + encapsulated;

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().toHex(), expected.toHex());
}

// Test EthernetIPv4Igmp_Invalid
TEST_F(Internal_EncapsulationTest, EthernetIPv4Igmp_Invalid)
{
    // Ethernet(14) + IPv4(20, proto=2 => IGMP) + incomplete IGMP(4) = 38 bytes
    ByteString ethernetHeader = std::string(
        "\xAA\xAA\xAA\xAA\xAA\xAA"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType = IPv4 (0x0800)
        14
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x00\x14"                    // Total Length = 20
        "\x00\x08"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x02"                    // TTL=64, Protocol=2 (IGMP)
        "\x00\x00"                    // Header checksum
        "\xC0\xA8\x01\x0F"            // Source IP
        "\xC0\xA8\x01\x10",           // Destination IP
        20
    );

    ByteString igmpHeader = std::string(
        "\x11\x64"                    // Type=17 (IGMPv3 Membership Report), Max Resp Time=100
        "\x12\x34"                    // Checksum
        "\x00\x02",                   // Multicast Address (incomplete)
        4
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xAA\xAA\xAA\xAA\xAA", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x01", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x00", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x01", 4); // 192.168.1.1
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x02", 4); // 192.168.1.2
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(ipv4);

    IgmpHeader igmp;
    igmp.type = std::string("\x11", 1);         // Type=17 (IGMPv3 Membership Report)
    igmp.maxRestTime = std::string("\x64", 1);  // Max Resp Time=100
    igmp.checksum = std::string("\x12\x34", 2); // Checksum
    igmp.multicastAddress = std::string("\x00\x02", 2); // Multicast Address (incomplete)
    igmp.v3.supress = std::string("\x00", 1); // Suppress
    igmp.v3.qrv = std::string("\x01", 1);     // Querier's Robustness Variable
    igmp.v3.qqic = std::string("\x00", 1);    // Querier's Query Interval Code
    igmp.v3.numSrc = std::string("\x03", 1);  // Number of Sources
    // Incomplete options
    packetInfo.Layer3.push_back(igmp);

    ByteString encapsulated = ByteString(""); // No payload for IGMP

    auto result = encapsulate(packetInfo, encapsulated);

    EXPECT_FALSE(result.has_value());
}

//--------------------------------------------------------------------------------
// 8. Ethernet + IPv4 + TCP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Tcp_Valid
TEST_F(Internal_EncapsulationTest, EthernetIPv4Tcp_Valid)
{
    // Ethernet(14) + IPv4(20, proto=6 => TCP) + TCP(20) = 54 bytes
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType = IPv4 (0x0800)
        14
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x00\x47"                    // Total Length = 40
        "\x00\x01"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x06"                    // TTL=64, Protocol=6 (TCP)
        "\xB7\x3C"                    // Header checksum
        "\xC0\xA8\x01\x11"            // Source IP
        "\xC0\xA8\x01\x12",           // Destination IP
        20
    );

    ByteString tcpHeader = std::string(
        "\x00\x50"                    // Source Port = 80
        "\x01\xBB"                    // Destination Port = 443
        "\x00\x00\x00\x01"            // Sequence Number
        "\x00\x00\x00\x00"            // Acknowledgment Number
        "\x50\x02"                    // Flags=SYN
        "\x71\x10"                    // Window Size
        "\x49\xDA"                    // Checksum
        "\x00\x00",                   // Urgent Pointer
        20
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x01", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x06", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x11", 4); // 192.168.1.17
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x12", 4); // 192.168.1.18
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(ipv4);

    TcpHeader tcp;
    tcp.sourcePort = std::string("\x00\x50", 2);        // Port 80
    tcp.destinationPort = std::string("\x01\xBB", 2);   // Port 443
    tcp.sequenceNumber = std::string("\x00\x00\x00\x01", 4); // Sequence Number
    tcp.ackNumber = std::string("\x00\x00\x00\x00", 4);     // Acknowledgment Number
    tcp.headerLength = std::string("\x50", 1);             // Data Offset=5
    tcp.windowSize = std::string("\x71\x10", 2);           // Window Size
    tcp.checksum = std::string("\x12\x34", 2);             // Checksum
    tcp.urgentPointer = std::string("\x00\x00", 2);        // Urgent Pointer
    tcp.flags.congestionWindowReduced = "0"; // Window Reduced
    tcp.flags.ecnEcho =                 "0"; // Ecn Echo
    tcp.flags.urgent =                  "0"; // Urgent
    tcp.flags.acknowledgement =         "0"; // Acknowledgment
    tcp.flags.push =                    "0"; // Push
    tcp.flags.reset =                   "0"; // Reset
    tcp.flags.syn =                     "1"; // Syn
    tcp.flags.fin =                     "0"; // Fin
    // No TCP options for simplicity
    packetInfo.Layer4.push_back(tcp);

    ByteString encapsulated = ByteString("HTTP GET /index.html HTTP/1.1\r\n");

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet + IPv4 + TCP + Payload
    ByteString expected = ethernetHeader + ipv4Header + tcpHeader + encapsulated;

    EXPECT_EQ(result.value().toHex(), expected.toHex());
}

// Test EthernetIPv4Tcp_Invalid
TEST_F(Internal_EncapsulationTest, EthernetIPv4Tcp_Invalid)
{
    // Incomplete TCP header
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType = IPv4 (0x0800)
        14
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x00\x47"                    // Total Length = 40
        "\x00\x01"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x06"                    // TTL=64, Protocol=6 (TCP)
        "\xB7\x3C"                    // Header checksum
        "\xC0\xA8\x01\x11"            // Source IP
        "\xC0\xA8\x01\x12",           // Destination IP
        20
    );

    ByteString tcpHeader = std::string(
        "\x00\x50"                    // Source Port = 80
        "\x01\xBB"                    // Destination Port = 443
        "\x00\x00\x00\x01"            // Sequence Number
        "\x00\x00\x00\x00"            // Acknowledgment Number
        "\x50\x02"                    // Flags=SYN
        "\x71\x10"                    // Window Size
        "\x49\xDA"                    // Checksum
        "\x00\x00",                   // Urgent Pointer
        20
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x01", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x06", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x11", 4); // 192.168.1.17
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x12", 4); // 192.168.1.18
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(ipv4);

    TcpHeader tcp;
    tcp.sourcePort = std::string("\x00\x50", 2);        // Port 80
    tcp.destinationPort = std::string("\x01\xBB", 2);   // Port 443
    tcp.sequenceNumber = std::string("\x00\x00\x00\x01", 4); // Sequence Number
    tcp.ackNumber = std::string("\x00\x00\x00\x00", 4);     // Acknowledgment Number
    tcp.headerLength = std::string("\x50", 1);             // Data Offset=5
    tcp.windowSize = std::string("\x71\x10", 2);           // Window Size
    tcp.checksum = std::string("\x12\x34", 2);             // Checksum
    tcp.urgentPointer = std::string("\x00\x00", 2);        // Urgent Pointer
    tcp.flags.congestionWindowReduced = "0"; // Window Reduced
    tcp.flags.ecnEcho =                 "0"; // Ecn Echo
    tcp.flags.urgent =                  "0"; // Urgent
    tcp.flags.acknowledgement =         "0"; // Acknowledgment
    tcp.flags.push =                    "0"; // Push
    tcp.flags.reset =                   "0"; // Reset
    packetInfo.Layer4.push_back(tcp);
    // Incomplete TCP flags
    ByteString encapsulated = ByteString("Incomplete HTTP Payload");

    auto result = encapsulate(packetInfo, encapsulated);

    EXPECT_FALSE(result.has_value());
}

//--------------------------------------------------------------------------------
// Ethernet + IPv4 + UDP
//--------------------------------------------------------------------------------

// Test EthernetIPv4Udp_Valid
TEST_F(Internal_EncapsulationTest, EthernetIPv4Udp_Valid)
{
    // Ethernet(14) + IPv4(20, proto=17 => UDP) + UDP(8) = 42 bytes
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType = IPv4 (0x0800)
        14
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x00\x28"                    // Total Length = 40
        "\x00\x0B"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x11"                    // TTL=64, Protocol=17 (UDP)
        "\xB7\x3E"                    // Header checksum
        "\xC0\xA8\x01\x15"            // Source IP
        "\xC0\xA8\x01\x16",           // Destination IP
        20
    );

    ByteString udpHeader = std::string(
        "\x00\x35"                    // Source Port = 53
        "\x00\x43"                    // Destination Port = 67
        "\x00\x14"                    // Length = 8
        "\xA4\x9C",                   // Checksum
        8
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x0B", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x11", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x15", 4); // 192.168.1.21
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x16", 4); // 192.168.1.22
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(ipv4);

    UdpHeader udp;
    udp.sourcePort = std::string("\x00\x35", 2);        // Port 53
    udp.destinationPort = std::string("\x00\x43", 2);   // Port 67
    udp.length = std::string("\x00\x08", 2);            // Length=8
    udp.checksum = std::string("\x12\x34", 2);          // Checksum
    packetInfo.Layer4.push_back(udp);

    ByteString encapsulated = ByteString("DHCP Payload");

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet + IPv4 + UDP + Payload
    ByteString expected = ethernetHeader + ipv4Header + udpHeader + encapsulated;

    EXPECT_EQ(result.value().toHex(), expected.toHex());
}

// Test EthernetIPv4Udp_Invalid
TEST_F(Internal_EncapsulationTest, EthernetIPv4Udp_Invalid)
{
    // Ethernet(14) + IPv4(20, proto=17 => UDP) + incomplete UDP(4) = 38 bytes
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType = IPv4 (0x0800)
        14
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x00\x28"                    // Total Length = 40
        "\x00\x0B"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x11"                    // TTL=64, Protocol=17 (UDP)
        "\xB7\x3E"                    // Header checksum
        "\xC0\xA8\x01\x15"            // Source IP
        "\xC0\xA8\x01\x16",           // Destination IP
        20
    );

    ByteString udpHeader = std::string(
        "\x00\x35"                    // Source Port = 53
        "\x00\x43"                    // Destination Port = 67
        "\x00\x14",                   // Length = 8
        6
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x0B", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x11", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x15", 4); // 192.168.1.21
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x16", 4); // 192.168.1.22
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(ipv4);

    UdpHeader udp;
    udp.sourcePort = std::string("\x00\x35", 2);        // Port 53
    udp.destinationPort = std::string("\x00\x43", 2);   // Port 67
    udp.length = std::string("\x00\x08", 2);            // Length=8
    // Incomplete UDP header
    packetInfo.Layer4.push_back(udp);

    ByteString encapsulated = ByteString("Incomplete DHCP Payload");

    auto result = encapsulate(packetInfo, encapsulated);

    EXPECT_FALSE(result.has_value());
}

//--------------------------------------------------------------------------------
// Ethernet + IPv4 + UDP + DHCP
//--------------------------------------------------------------------------------

// Test EthernetIPv4UdpDhcp_Valid
TEST_F(Internal_EncapsulationTest, EthernetIPv4UdpDhcp_Valid)
{
    // Ethernet(14) + IPv4(20, proto=17 => UDP) + UDP(8) + DHCP(248)
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType = IPv4 (0x0800)
        14
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x01\x26"                    // Total Length = 234
        "\x00\x0D"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x11"                    // TTL=64, Protocol=17 (UDP)
        "\xB6\x36"                    // Header checksum
        "\xC0\xA8\x01\x19"            // Source IP
        "\xC0\xA8\x01\x1A",           // Destination IP
        20
    );

    ByteString udpHeader = std::string(
        "\x00\x43"                    // Source Port = 67
        "\x00\x44"                    // Destination Port = 68
        "\x01\x12"                    // Length
        "\x58\x3A",                   // Checksum
        8
    );

    // Build DHCP Header
    // DHCP Packet structure is complex; here, we'll construct a simplified version
    ByteString dhcpHeader = std::string(
        "\x02"                      // Message type: Boot Reply
        "\x01"                      // Hardware type: Ethernet
        "\x06"                      // Hardware address length
        "\x00"                      // Hops
        "\x39\x03\xf3\x26"          // Transaction ID
        "\x00\x00"                  // Seconds elapsed
        "\x80\x00"                  // BOOTP flags: Broadcast
        "\x00\x00\x00\x00"          // Client IP address
        "\xC0\xA8\x00\x64"          // Your (client) IP address
        "\xC0\xA8\x00\x01"          // Next server IP address
        "\xC0\xA8\x00\x02"          // Relay agent IP address
        "\x00\x1A\x2B\x3C\x4D\x5E"  // Client hardware address (MAC)
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"  // Client hardware address padding
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"  // Server host name
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"  // Boot File
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x63\x82\x53\x63"          // Magic Cookie
        "\x35\x01\x02"             // DHCP Option 53 (Message Type: DHCP Offer)
        "\xFF"                    // End Option
        "\x00\x00\x00\x00",  // Padding
        248
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x0D", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x11", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x19", 4); // 192.168.1.25
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x1A", 4); // 192.168.1.26
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(ipv4);

    UdpHeader udp;
    udp.sourcePort = std::string("\x00\x43", 2);        // Port 67
    udp.destinationPort = std::string("\x00\x44", 2);   // Port 68
    udp.length = std::string("\x01\x0C", 2);            // Length=234
    udp.checksum = std::string("\x12\x34", 2);          // Checksum
    packetInfo.Layer4.push_back(udp);

    DhcpHeader dhcp;
    dhcp.boot = std::string("\x02", 1);                         // Boot Reply
    dhcp.hardwareType = std::string("\x01", 1);                 // Ethernet
    dhcp.hardwareAddressLength = std::string("\x06", 1);        // MAC length
    dhcp.hops = std::string("\x00", 1);                          // Hops
    dhcp.transID = std::string("\x39\x03\xF3\x26", 4);          // Transaction ID
    dhcp.secondsElapsed = std::string("\x00\x00", 2);           // Seconds elapsed
    dhcp.bootpFlags.broadcast = "1";                            // Broadcast flag
    dhcp.bootpFlags.reserved = "000000000000000";               // Reserved
    dhcp.clientIP = std::string("\x00\x00\x00\x00", 4);         // Client IP
    dhcp.yourClientIP = std::string("\xC0\xA8\x00\x64", 4);     // Your (Client) IP
    dhcp.nextServerIP = std::string("\xC0\xA8\x00\x01", 4);     // Next Server IP
    dhcp.relayAgentIP = std::string("\xC0\xA8\x00\x02", 4);     // Relay Agent IP
    dhcp.clientMacAddress = std::string("\x00\x1A\x2B\x3C\x4D\x5E", 6); // Client MAC
    dhcp.clientHardwareAddressPadding = ByteString(10, '\x00'); // Hardware address padding
    dhcp.serverHostName = ByteString(64, '\x00');               // Server Hostname
    dhcp.bootFile = ByteString(128, '\x00');                    // Boot File
    dhcp.magicCookie = ByteString("\x63\x82\x53\x63");
    dhcp.options.push_back({ByteString("\x35", 1), ByteString("\x01", 1), ByteString("\x02", 1)});
    dhcp.end = ByteString("\xFF", 1);
    dhcp.padding = ByteString(4, '\x00');

    packetInfo.Layer5.push_back(dhcp);

    ByteString encapsulated = ByteString("DHCP Offer Payload");

    auto result = encapsulate(packetInfo, encapsulated);

    // Expected Ethernet + IPv4 + UDP + DHCP + Payload
    ByteString expected = ethernetHeader + ipv4Header + udpHeader + dhcpHeader + encapsulated;

    EXPECT_EQ(result.value().toHex(), expected.toHex());
}

// Test EthernetIPv4UdpDhcp_Invalid
TEST_F(Internal_EncapsulationTest, EthernetIPv4UdpDhcp_Invalid)
{
    // Ethernet(14) + IPv4(20, proto=17 => UDP) + UDP(8) + incomplete DHCP
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType = IPv4 (0x0800)
        14
    );

    ByteString ipv4Header = std::string(
        "\x45\x00"                    // Version/IHL, Type of Service
        "\x01\x26"                    // Total Length = 234
        "\x00\x0D"                    // Identification
        "\x40\x00"                    // Flags, Fragment Offset
        "\x40\x11"                    // TTL=64, Protocol=17 (UDP)
        "\xB6\x36"                    // Header checksum
        "\xC0\xA8\x01\x19"            // Source IP
        "\xC0\xA8\x01\x1A",           // Destination IP
        20
    );

    ByteString udpHeader = std::string(
        "\x00\x43"                    // Source Port = 67
        "\x00\x44"                    // Destination Port = 68
        "\x01\x12"                    // Length
        "\x06\x8C",                   // Checksum
        8
    );

    // Build DHCP Header
    // DHCP Packet structure is complex; here, we'll construct a simplified version
    ByteString dhcpHeader = std::string(
        "\x02"                      // Message type: Boot Reply
        "\x01"                      // Hardware type: Ethernet
        "\x06"                      // Hardware address length
        "\x00"                      // Hops
        "\x39\x03\xf3\x26"          // Transaction ID
        "\x00\x00"                  // Seconds elapsed
        "\x80\x00"                  // BOOTP flags: Broadcast
        "\x00\x00\x00\x00"          // Client IP address
        "\xC0\xA8\x00\x64"          // Your (client) IP address
        "\xC0\xA8\x00\x01"          // Next server IP address
        "\xC0\xA8\x00\x02"          // Relay agent IP address
        "\x00\x1A\x2B\x3C\x4D\x5E"  // Client hardware address (MAC)
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"  // Client hardware address padding
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"  // Server host name
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"  // Boot File
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x63\x82\x53\x63"          // Magic Cookie
        "\x35\x01\x02"             // DHCP Option 53 (Message Type: DHCP Offer)
        "\xFF"                    // End Option
        "\x00\x00\x00\x00\x00",  // Padding
        248
    );

    // Assemble the packet
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
    ipv4.identification = std::string("\x00\x0D", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x11", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x19", 4); // 192.168.1.25
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x1A", 4); // 192.168.1.26
    // Initialize fragment flags and options as needed
    packetInfo.Layer3.push_back(ipv4);

    UdpHeader udp;
    udp.sourcePort = std::string("\x00\x43", 2);        // Port 67
    udp.destinationPort = std::string("\x00\x44", 2);   // Port 68
    udp.length = std::string("\x01\x0C", 2);            // Length=234
    udp.checksum = std::string("\x12\x34", 2);          // Checksum
    packetInfo.Layer4.push_back(udp);

    DhcpHeader dhcp;
    dhcp.boot = std::string("\x02", 1);                         // Boot Reply
    dhcp.hardwareType = std::string("\x01", 1);                 // Ethernet
    dhcp.hardwareAddressLength = std::string("\x06", 1);        // MAC length
    dhcp.hops = std::string("\x00", 1);                          // Hops
    dhcp.transID = std::string("\x39\x03\xF3\x26", 4);          // Transaction ID
    dhcp.secondsElapsed = std::string("\x00\x00", 2);           // Seconds elapsed
    dhcp.bootpFlags.broadcast = "1";                            // Broadcast flag
    dhcp.bootpFlags.reserved = "0000000";                       // Reserved
    dhcp.clientIP = std::string("\x00\x00\x00\x00", 4);         // Client IP
    dhcp.yourClientIP = std::string("\xC0\xA8\x00\x64", 4);     // Your (Client) IP
    dhcp.nextServerIP = std::string("\xC0\xA8\x00\x01", 4);     // Next Server IP
    dhcp.relayAgentIP = std::string("\xC0\xA8\x00\x02", 4);     // Relay Agent IP
    dhcp.clientMacAddress = std::string("\x00\x1A\x2B\x3C\x4D\x5E", 6); // Client MAC
    dhcp.clientHardwareAddressPadding = ByteString(10, '\x00'); // Hardware address padding
    dhcp.serverHostName = ByteString(64, '\x00');               // Server Hostname
    dhcp.bootFile = ByteString(128, '\x00');                    // Boot File
    dhcp.options.push_back({ByteString("\x35", 1), ByteString("\x01", 1), ByteString("\x02", 1)});
    dhcp.end = ByteString("\xFF", 1);
    dhcp.padding = ByteString(5, '\x00');
    // Incomplete header missing cookie
    packetInfo.Layer5.push_back(dhcp);

    ByteString encapsulated = ByteString("DHCP Offer Payload");

    auto result = encapsulate(packetInfo, encapsulated);

    EXPECT_FALSE(result.has_value());
}

// Test EthernetIPv4Eigrp_Valid
TEST_F(Internal_EncapsulationTest, EthernetIPv4Eigrp_Valid)
{
    // Build Ethernet Header
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType: IPv4 (0x0800)
        14
    );

    // Build IPv4 Header
    ByteString ipv4Header = std::string(
        "\x45\x00"                  // Vesion/IHL, type of service
        "\x00\x30"                  // Total Length: 48 (20 + 28 for EIGRP)
        "\x00\x0D"                  // Identification
        "\x40\x00"                  // Flags, Fragment Offset
        "\x40\x58"                  // TTL = 64, Protocol = EIGRP (88)
        "\xB6\xD7"                  // Checksum
        "\xC0\xA8\x01\x20"          // Source IP: 192.168.1.32
        "\xC0\xA8\x01\x21",         // Destination IP: 192.168.1.33
        20
    );

    // Build EIGRP Header
    ByteString eigrpHeader = std::string(
        "\x01"                      // Version
        "\x05"                      // Opcode (Hello)
        "\x5D\xED"                  // Checksum (placeholder)
        "\x00\x00\x00\x00"          // Flags (all unset)
        "\x00\x00\x00\x01"          // Sequence number
        "\x00\x00\x00\x02"          // Acknowledgment number
        "\x00\x01"                  // Virtual Router ID
        "\x00\x64"                  // Autonomous System number
        "\x01\x04\xDE\xAD"          // Option 1 (Type=1, Length=4, Value=DEAD)
        "\x02\x04\xBE\xEF",         // Option 2 (Type=2, Length=4, Value=BEEF)
        28
    );

    // Assemble the full expected packet
    ByteString expected = ethernetHeader + ipv4Header + eigrpHeader;

    // Create PacketInfo and add headers
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x34", 2); // Total Length
    ipv4.identification = std::string("\x00\x0D", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x58", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x20", 4); // 192.168.1.32
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x21", 4); // 192.168.1.33
    packetInfo.Layer3.push_back(ipv4);

    EigrpHeader eigrp;
    eigrp.version = std::string("\x01", 1);                  // Version
    eigrp.opcode = std::string("\x05", 1);                   // Opcode (Hello)
    eigrp.checksum = std::string("\x00\x00", 2);             // Checksum placeholder
    eigrp.flags.endOfTable = "0";
    eigrp.flags.restart = "0";
    eigrp.flags.conditionalRecieve = "0";
    eigrp.flags.init = "0";
    eigrp.sequence = std::string("\x00\x00\x00\x01", 4);     // Sequence number
    eigrp.ack = std::string("\x00\x00\x00\x02", 4);          // Acknowledgment number
    eigrp.virtualRouterID = std::string("\x00\x01", 2);      // Virtual Router ID
    eigrp.autonomousSystem = std::string("\x00\x64", 2);     // Autonomous System number
    eigrp.options.push_back({std::string("\x01", 1), std::string("\x04", 1), std::string("\xDE\xAD", 2)});
    eigrp.options.push_back({std::string("\x02", 1), std::string("\x04", 1), std::string("\xBE\xEF", 2)});
    packetInfo.Layer4.push_back(eigrp);

    ByteString payload = std::string("");  // No additional payload for this test
    auto result = encapsulate(packetInfo, payload);

    // Validate the encapsulation
    EXPECT_EQ(result.value().toHex(), expected.toHex());
}

// Test EthernetIPv4Eigrp_Invalid
TEST_F(Internal_EncapsulationTest, EthernetIPv4Eigrp_Invalid)
{
    // Build Ethernet Header
    ByteString ethernetHeader = std::string(
        "\xAA\xBB\xCC\xDD\xEE\xFF"  // Destination MAC
        "\x11\x22\x33\x44\x55\x66"  // Source MAC
        "\x08\x00",                 // EtherType: IPv4 (0x0800)
        14
    );

    // Build IPv4 Header
    ByteString ipv4Header = std::string(
        "\x45\x00"                  // Vesion/IHL, type of service
        "\x00\x30"                  // Total Length: 48 (20 + 28 for EIGRP)
        "\x00\x0D"                  // Identification
        "\x40\x00"                  // Flags, Fragment Offset
        "\x40\x58"                  // TTL = 64, Protocol = EIGRP (88)
        "\xB6\xD7"                  // Checksum
        "\xC0\xA8\x01\x20"          // Source IP: 192.168.1.32
        "\xC0\xA8\x01\x21",         // Destination IP: 192.168.1.33
        20
    );

    // Build Invalid EIGRP Header
    ByteString eigrpHeader = std::string(
        "\x01"                      // Version
        "\x05"                      // Opcode (Hello)
        "\x5D\xED"                  // Checksum (placeholder)
        "\x00\x00\x00\x00"          // Flags (all unset)
        "\x00\x00\x00\x01"          // Sequence number
        "\x00\x00\x00\x02"          // Acknowledgment number
        "\x00\x01"                  // Virtual Router ID
        "\x00\x64"                  // Autonomous System number
        "\x01\x04\xDE\xAD"          // Option 1 (Type=1, Length=4, Value=DEAD)
        "\x02\x04\xBE\xEF",         // Option 2 (Type=2, Length=4, Value=BEEF)
        28
    );

    // Assemble the full expected packet
    ByteString expected = ethernetHeader + ipv4Header + eigrpHeader;

    // Create PacketInfo and add headers
    PacketInfo packetInfo;
    EthernetHeader eth;
    eth.destinationMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    eth.sourceMac = std::string("\x11\x22\x33\x44\x55\x66", 6);
    eth.type = std::string("\x08\x00", 2); // IPv4
    packetInfo.Layer2.push_back(eth);

    IPv4Header ipv4;
    ipv4.version = "4";
    ipv4.headerLength = "5";
    ipv4.serviceField = std::string("\x00", 1);
    ipv4.totalLength = std::string("\x00\x34", 2); // Total Length
    ipv4.identification = std::string("\x00\x0D", 2);
    ipv4.fragmentFlag.reserved = "0";
    ipv4.fragmentFlag.fragment = "1";
    ipv4.fragmentFlag.moreFragment = "0";
    ipv4.fragmentFlag.fragmentOffset = "0000000000000";
    ipv4.TTL = std::string("\x40", 1);
    ipv4.protocol = std::string("\x58", 1);
    ipv4.checksum = std::string("\x00\x00", 2);
    ipv4.sourceAddress = std::string("\xC0\xA8\x01\x20", 4); // 192.168.1.32
    ipv4.destinationAddress = std::string("\xC0\xA8\x01\x21", 4); // 192.168.1.33
    packetInfo.Layer3.push_back(ipv4);

    EigrpHeader eigrp;
    eigrp.version = std::string("\x01", 1);                  // Version
    eigrp.opcode = std::string("\x05", 1);                   // Opcode (Hello)
    eigrp.checksum = std::string("\x00\x00", 2);             // Checksum placeholder
    eigrp.sequence = std::string("\x00\x00\x00\x01", 4);     // Sequence number
    eigrp.ack = std::string("\x00\x00\x00\x02", 4);          // Acknowledgment number
    eigrp.virtualRouterID = std::string("\x00\x01", 2);      // Virtual Router ID
    eigrp.autonomousSystem = std::string("\x00\x64", 2);     // Autonomous System number
    // Missing flag information
    packetInfo.Layer4.push_back(eigrp);

    ByteString payload = std::string("");  // No additional payload for this test
    auto result = encapsulate(packetInfo, payload);

    // Validate the encapsulation
    EXPECT_FALSE(result.has_value());
}
