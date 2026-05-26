#include <gtest/gtest.h>
#include <HeaderHelpers.hpp>
#include <Checksums.h>
#include <cstring>

class Internal_ChecksumTest : public ::testing::Test {};

// Test Suite for calculateChecksum
TEST_F(Internal_ChecksumTest, CalculateChecksum_1Byte)
{
    uint8_t packet[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0 };
    uint8_t pseudo[2] = { 0x10, 0x20 };

    Checksum::calculateChecksum(packet, 5, 2, 1, pseudo, 2, false);
    // 1 byte checksum = sum of pseudo + packet excluding checksum byte
    EXPECT_EQ(packet[2], 0x3c);
}

TEST_F(Internal_ChecksumTest, CalculateChecksum_1Byte_ZeroPseudo)
{
    uint8_t packet[3] = { 0xAA, 0xBB, 0 };
    Checksum::calculateChecksum(packet, 3, 1, 1);
}

TEST_F(Internal_ChecksumTest, CalculateChecksum_2Byte)
{
    uint8_t packet[6] = { 0x12, 0x34, 0x00, 0x00, 0x56, 0x78 };
    uint8_t pseudo[4] = { 0xAA, 0xBB, 0xCC, 0xDD };

    Checksum::calculateChecksum(packet, 6, 2, 2, pseudo, 4, false);
    uint16_t csum = readU16(packet + 2);

    EXPECT_EQ(csum, 0x1FBA);
}

TEST_F(Internal_ChecksumTest, CalculateChecksum_2Byte_ZeroChecksum)
{
    uint8_t packet[4] = { 0x01, 0x02, 0x00, 0x00 };

    Checksum::calculateChecksum(packet, 4, 2, 2);
    uint16_t csum = readU16(packet + 2);
    EXPECT_EQ(csum, 0xFEFD);
}

TEST_F(Internal_ChecksumTest, CalculateChecksum_2Byte_Swap)
{
    uint8_t packet[6] = { 0x12, 0x34, 0x00, 0x00, 0x56, 0x78 };
    uint8_t pseudo[2] = { 0x00, 0x01 };

    Checksum::calculateChecksum(packet, 6, 2, 2, pseudo, 2, true);
    uint8_t a = packet[2], b = packet[3];
    Checksum::calculateChecksum(packet, 6, 2, 2, pseudo, 2, false);
    EXPECT_EQ(a, packet[3]);
    EXPECT_EQ(b, packet[2]);
}

TEST_F(Internal_ChecksumTest, CalculateChecksum_4Byte)
{
    uint8_t packet[12] = {
        0x01, 0x02, 0x03, 0x04,
        0x00, 0x00, 0x00, 0x00,
        0x05, 0x06, 0x07, 0x08
    };
    uint8_t pseudo[8] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };

    Checksum::calculateChecksum(packet, 12, 4, 4, pseudo, 8, false);
    uint32_t sum = readU32(packet + 4);
    EXPECT_EQ(sum, 0x99775533);
}

TEST_F(Internal_ChecksumTest, CalculateChecksum_4Byte_ZeroPseuod)
{
    uint8_t packet[12] = {
        0x01, 0x02, 0x03, 0x04,
        0x00, 0x00, 0x00, 0x00,
        0x05, 0x06, 0x07, 0x08
    };
    Checksum::calculateChecksum(packet, 8, 4, 4);
    uint32_t sum = readU32(packet + 4);
    EXPECT_EQ(sum, 0xFEFDFCFB);
}

TEST_F(Internal_ChecksumTest, CalculateChecksum_4Byte_Swap)
{
    uint8_t packet[12] = {
        0x01, 0x02, 0x03, 0x04,
        0x00, 0x00, 0x00, 0x00,
        0x05, 0x06, 0x07, 0x08
    };
    uint8_t pseudo[4] = { 0x01, 0x02, 0x03, 0x04 };

    Checksum::calculateChecksum(packet, 8, 4, 4, pseudo, 4, true);
    uint8_t a = packet[4], b = packet[5], c = packet[6], d = packet[7];
    Checksum::calculateChecksum(packet, 8, 4, 4, pseudo, 4, false);
    EXPECT_EQ(a, packet[7]);
    EXPECT_EQ(b, packet[6]);
    EXPECT_EQ(c, packet[5]);
    EXPECT_EQ(d, packet[4]);
}

TEST_F(Internal_ChecksumTest, CalculateChecksum_UnsupportedSize)
{
    uint8_t packet[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    uint8_t pseudo[2] = { 0xAA, 0xBB };

    Checksum::calculateChecksum(packet, 8, 2, 3, pseudo, 2, false);

    EXPECT_EQ(packet[2], 0);
    EXPECT_EQ(packet[3], 0);
    EXPECT_EQ(packet[4], 0);
}

TEST_F(Internal_ChecksumTest, CalculateChecksum_ChecksumRegionZeroedFirst)
{
    uint8_t packet[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t pseudo[2] = { 0x01, 0x02 };

    Checksum::calculateChecksum(packet, 6, 2, 2, pseudo, 2, false);
    uint16_t sum = readU16(packet + 2);
    EXPECT_EQ(sum, 0xFEFD);
}
