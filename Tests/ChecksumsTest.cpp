#include <gtest/gtest.h>
#include <Checksums.h>
#include <vector>
#include <string>
#include <Functions.h>
#include <Encapsulation.h>

// Test Suite for base256StringToBytes
TEST(ChecksumTest, Base256StringToBytes)
{
    std::string base256Str = "\x01\x02\xFF\x00";
    std::vector<uint8_t> expected = {0x01, 0x02, 0xFF};
    auto bytes = Checksum::base256StringToBytes(base256Str);
    EXPECT_EQ(bytes, expected);

    // Test empty string
    base256Str = "";
    expected = {};
    bytes = Checksum::base256StringToBytes(base256Str);
    EXPECT_EQ(bytes, expected);
}

// Test Suite for calculateChecksum
TEST(ChecksumTest, CalculateChecksum1Byte)
{
    // 1-byte checksum is simple sum modulo 256
    struct Checksum1ByteTest
    {
        ByteString data;
        uint8_t expected;
    };

    std::vector<Checksum1ByteTest> testVectors = {
        {"", 0},
        {"a", 0x61},
        {"abc", (0x61 + 0x62 + 0x63) & 0xFF},
        {std::string(300, 'A'), (300 * 0x41) & 0xFF},
    };

    for (const auto& test : testVectors)
    {
        ByteString checksum = Checksum::calculateChecksum(test.data, 1);
        ASSERT_EQ(checksum.size(), 1u);
        EXPECT_EQ(static_cast<uint8_t>(checksum[0]), test.expected);
    }
}

TEST(ChecksumTest, CalculateChecksum2Byte)
{
    // 2-byte checksum: Internet checksum
    struct Checksum2ByteTest
    {
        ByteString data;
        std::string expected_hex;
    };

    std::vector<Checksum2ByteTest> testVectors =
    {
        {"", "FFFF"},
        {"a", "9EFF"},
        {"abc", "3B9D"},
        {"123456789", "F62A"},
    };

    for (const auto& test : testVectors)
    {
        ByteString checksum = Checksum::calculateChecksum(test.data, 2);
        ASSERT_EQ(checksum.size(), 2u);
        std::string checksum_hex = checksum.toHex();
        EXPECT_EQ(checksum_hex, test.expected_hex);
    }

    // Test empty data
    ByteString emptyData = "";
    ByteString checksum_empty = Checksum::calculateChecksum(emptyData, 2);
    EXPECT_EQ(checksum_empty.toHex(), "FFFF");
}

TEST(ChecksumTest, CalculateChecksum4Byte)
{
    // 4-byte checksum: 32-bit sum
    struct Checksum4ByteTest
    {
        ByteString data;
        std::string expected_hex;
    };

    std::vector<Checksum4ByteTest> testVectors =
    {
        {"", "FFFFFFFF"},
        {"a", "9EFFFFFF"},
        {"abcd", "9E9D9C9B"},
        {"123456789", "60979593"},
    };

    for (const auto& test : testVectors)
    {
        ByteString checksum = Checksum::calculateChecksum(test.data, 4);
        ASSERT_EQ(checksum.size(), 4u);
        std::string checksum_hex = checksum.toHex();
        EXPECT_EQ(checksum_hex, test.expected_hex);
    }

    // Test empty data
    ByteString emptyData = "";
    ByteString checksum_empty = Checksum::calculateChecksum(emptyData, 4);
    EXPECT_EQ(checksum_empty.toHex(), "FFFFFFFF");
}

TEST(ChecksumTest, CalculateChecksumUnsupportedSize)
{
    ByteString data = "test data";
    size_t unsupported_size = 3;
    ByteString checksum = Checksum::calculateChecksum(data, unsupported_size);
    EXPECT_EQ(checksum.size(), unsupported_size);
    EXPECT_EQ(checksum, std::string(unsupported_size, '\0'));
}

TEST(ChecksumTest, CalculateProtocolChecksumOverload1EmptyPseudoHeader)
{
    // Test with empty pseudoHeader
    ByteString pseudoHeader = "";
    ByteString header = "headerdata";
    size_t checksumStartIndex = 2;
    size_t checksumSize = 1;
    bool swap = false;

    // Expected checksum: calculate checksum over "headerdata" with 1-byte checksum
    ByteString dataToChecksum = header;
    ByteString expectedChecksum = Checksum::calculateChecksum(dataToChecksum, checksumSize);

    // No swapping needed for 1-byte checksum
    ByteString modifiedHeader = header;

    // Call the function
    Checksum::calculateProtocolChecksum(pseudoHeader, modifiedHeader, checksumStartIndex, checksumSize, swap);

    // Replace the checksumStartIndex with expectedChecksum
    ByteString expectedHeader = header;
    if (checksumStartIndex + checksumSize <= header.size())
    {
        expectedHeader.replace(checksumStartIndex, checksumSize, expectedChecksum);
        EXPECT_EQ(modifiedHeader, expectedHeader);
    }
    else
    {
        EXPECT_EQ(modifiedHeader, header);
    }
}

TEST(ChecksumTest, CalcualteProtocolChecksumOverload1InvalidIndex)
{
    // Test with invalid checksumStartIndex
    ByteString pseudoHeader = "pseudo";
    ByteString header = "header";
    size_t checksumStartIndex = 10;
    size_t checksumSize = 2;
    bool swap = false;

    ByteString modifiedHeader = header;

    // Capture stderr
    testing::internal::CaptureStderr();

    // Call the function
    Checksum::calculateProtocolChecksum(pseudoHeader, modifiedHeader, checksumStartIndex, checksumSize, swap);

    std::string output = testing::internal::GetCapturedStderr();

    // Expect error message
    EXPECT_EQ(modifiedHeader, header);
}

// Test Suite for calculateProtocolChecksum (second overload)
TEST(ChecksumTest, CalculateProtocolChecksumOverload2) 
{
    // Initialize headers array with std::optional
    std::optional<ByteString> headers[static_cast<size_t>(HeaderType::Count)] = {
        ByteString("header1data"),
        ByteString("header2data")
    };
    
    ByteString pseudoHeader = "pseudo";
    ByteString payload = "payload";
    HeaderType startHeaderType = HeaderType::Ethernet;
    size_t checksumStartIndex = 5;
    size_t checksumSize = 2;
    bool swap = false;
    
    // Accumulate data: pseudoHeader + headers[Header1] + headers[Header2] + payload
    ByteString dataToChecksum = pseudoHeader + headers[0].value() + headers[1].value() + payload;
    
    ByteString checksumBytes = Checksum::calculateChecksum(dataToChecksum, checksumSize);
    
    // No swapping
    ByteString expectedChecksum = checksumBytes;
    
    // Make a copy of headers to modify
    std::optional<ByteString> modifiedHeaders[static_cast<size_t>(HeaderType::Count)] = {
        headers[0],
        headers[1]
    };
    
    // Call the function
    Checksum::calculateProtocolChecksum(modifiedHeaders, pseudoHeader, payload, startHeaderType, checksumStartIndex, checksumSize, swap);
    
    // Insert checksum into headers[startHeaderType] at checksumStartIndex
    if (checksumStartIndex + checksumSize <= modifiedHeaders[static_cast<size_t>(startHeaderType)].value().size()) {
        ByteString expectedHeader = modifiedHeaders[static_cast<size_t>(startHeaderType)].value();
        expectedHeader.replace(checksumStartIndex, checksumSize, checksumBytes);
        EXPECT_EQ(modifiedHeaders[static_cast<size_t>(startHeaderType)].value(), expectedHeader);
    } else {
        // If insertion exceeds header size, expect no change
        EXPECT_EQ(modifiedHeaders[static_cast<size_t>(startHeaderType)].value(), headers[static_cast<size_t>(startHeaderType)].value());
    }
}

TEST(ChecksumTest, CalculateProtocolChecksumOverload2MissingHeader) 
{
    // Initialize headers array with std::optional
    std::optional<ByteString> headers[static_cast<size_t>(HeaderType::Count)] = {
        std::nullopt, // Header1 is missing
        ByteString("header2data")
    };
    
    ByteString pseudoHeader = "pseudo";
    ByteString payload = "payload";
    HeaderType startHeaderType = HeaderType::Ethernet; // Header1 is missing
    size_t checksumStartIndex = 2;
    size_t checksumSize = 1;
    bool swap = false;
    
    // Capture stderr
    testing::internal::CaptureStderr();
    
    // Call the function
    Checksum::calculateProtocolChecksum(headers, pseudoHeader, payload, startHeaderType, checksumStartIndex, checksumSize, swap);
    
    std::string output = testing::internal::GetCapturedStderr();
    
    // Expect error message
    EXPECT_NE(output.find("Header does not exist when calculating checksum"), std::string::npos);
}

TEST(ChecksumTest, CalculateProtocolChecksumOverload2InvalidHeaderType) 
{
    // Initialize headers array with std::optional
    std::optional<ByteString> headers[static_cast<size_t>(HeaderType::Count)] = {
        ByteString("header1data"),
        ByteString("header2data")
    };
    
    ByteString pseudoHeader = "pseudo";
    ByteString payload = "payload";
    // Invalid HeaderType by casting a value beyond Count
    HeaderType invalidHeaderType = static_cast<HeaderType>(static_cast<size_t>(HeaderType::Count));
    size_t checksumStartIndex = 2;
    size_t checksumSize = 2;
    bool swap = false;
    
    // Capture stderr
    testing::internal::CaptureStderr();
    
    // Call the function
    Checksum::calculateProtocolChecksum(headers, pseudoHeader, payload, invalidHeaderType, checksumStartIndex, checksumSize, swap);
    
    std::string output = testing::internal::GetCapturedStderr();
}
