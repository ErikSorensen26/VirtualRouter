// Internal_CommandSerializerTest.cpp

#if 0

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>
#include <string>
#include <memory>

#include "configs/serializer/CommandPath.hpp"
#include "configs/serializer/ConfigSerializer.hpp"
#include "configs/serializer/ConfigWriter.hpp"
#include "configs/serializer/ValueFormat.hpp"
#include "cli/tree/CommandTree.h"
#include "cli/tree/nodes/Command.h"

using namespace config::serializer;
using namespace testing;

// ============================================================================
// ConfigWriter Tests
// ============================================================================

class ConfigWriterTest : public ::testing::Test {};

TEST_F(ConfigWriterTest, SingleLineWrite)
{
    ConfigWriter writer;
    writer.line("router ospf 1");

    EXPECT_EQ(writer.str(), "router ospf 1\n");
}

TEST_F(ConfigWriterTest, MultipleLines)
{
    ConfigWriter writer;
    writer.line("router ospf 1");
    writer.line("area 0 range 10.0.0.0 255.255.255.0");

    std::string expected = "router ospf 1\narea 0 range 10.0.0.0 255.255.255.0\n";
    EXPECT_EQ(writer.str(), expected);
}

TEST_F(ConfigWriterTest, IndentedScope)
{
    ConfigWriter writer;
    writer.line("router ospf 1");
    {
        ConfigWriter::Scope scope = writer.scope();
        writer.line("area 0 range 10.0.0.0 255.255.255.0");
    }
    writer.line("router bgp 65000");

    std::string expected = "router ospf 1\n area 0 range 10.0.0.0 255.255.255.0\nrouter bgp 65000\n";
    EXPECT_EQ(writer.str(), expected);
}

TEST_F(ConfigWriterTest, NestedScopes)
{
    ConfigWriter writer;
    writer.line("router ospf 1");
    {
        ConfigWriter::Scope s1 = writer.scope();
        writer.line("area 1");
        {
            ConfigWriter::Scope s2 = writer.scope();
            writer.line("range 10.0.0.0 255.255.255.0");
        }
    }

    std::string expected = "router ospf 1\n area 1\n  range 10.0.0.0 255.255.255.0\n";
    EXPECT_EQ(writer.str(), expected);
}

TEST_F(ConfigWriterTest, CustomIndentWidth)
{
    ConfigWriter writer(4);
    writer.line("router ospf 1");
    {
        ConfigWriter::Scope scope = writer.scope();
        writer.line("area 0");
    }

    std::string expected = "router ospf 1\n    area 0\n";
    EXPECT_EQ(writer.str(), expected);
}

TEST_F(ConfigWriterTest, BlankLineInsertion)
{
    ConfigWriter writer;
    writer.line("router ospf 1");
    writer.blank();
    writer.line("router bgp 65000");

    std::string expected = "router ospf 1\n\nrouter bgp 65000\n";
    EXPECT_EQ(writer.str(), expected);
}

TEST_F(ConfigWriterTest, VectorOfWordsJoin)
{
    ConfigWriter writer;
    std::vector<std::string> words = {"ip", "address", "10.0.0.1"};
    writer.line(words);

    EXPECT_EQ(writer.str(), "ip address 10.0.0.1\n");
}

TEST_F(ConfigWriterTest, VectorOfWordsIndented)
{
    ConfigWriter writer;
    writer.line("interface GigabitEthernet 0/0");
    {
        ConfigWriter::Scope scope = writer.scope();
        std::vector<std::string> words = {"ip", "address", "10.0.0.1", "255.255.255.0"};
        writer.line(words);
    }

    std::string expected = "interface GigabitEthernet 0/0\n ip address 10.0.0.1 255.255.255.0\n";
    EXPECT_EQ(writer.str(), expected);
}

TEST_F(ConfigWriterTest, ScopePopRestoresDepth)
{
    ConfigWriter writer;
    writer.line("level 0");
    {
        ConfigWriter::Scope s = writer.scope();
        writer.line("level 1");
    }
    writer.line("back to level 0");

    std::string expected = "level 0\n level 1\nback to level 0\n";
    EXPECT_EQ(writer.str(), expected);
}

TEST_F(ConfigWriterTest, DepthBoundary)
{
    ConfigWriter writer;
    writer.pop();
    writer.pop();
    writer.line("depth should not go negative");

    EXPECT_EQ(writer.str(), "depth should not go negative\n");
}

TEST_F(ConfigWriterTest, Take)
{
    ConfigWriter writer;
    writer.line("test");

    std::string taken = writer.take();
    EXPECT_EQ(taken, "test\n");
    EXPECT_EQ(writer.str(), "");
}

TEST_F(ConfigWriterTest, ZeroIndentWidth)
{
    ConfigWriter writer(0);
    writer.line("root");
    {
        ConfigWriter::Scope scope = writer.scope();
        writer.line("child");
    }

    std::string expected = "root\nchild\n";
    EXPECT_EQ(writer.str(), expected);
}

// ============================================================================
// ValueFormat Tests
// ============================================================================

class ValueFormatTest : public ::testing::Test
{
};

TEST_F(ValueFormatTest, FormatUint32)
{
    EXPECT_EQ(formatValue(uint32_t(42)), "42");
    EXPECT_EQ(formatValue(uint32_t(0)), "0");
    EXPECT_EQ(formatValue(uint32_t(65535)), "65535");
}

TEST_F(ValueFormatTest, FormatInt32)
{
    EXPECT_EQ(formatValue(int32_t(42)), "42");
    EXPECT_EQ(formatValue(int32_t(-42)), "-42");
}

TEST_F(ValueFormatTest, FormatUint8)
{
    EXPECT_EQ(formatValue(uint8_t(255)), "255");
    EXPECT_EQ(formatValue(uint8_t(0)), "0");
}

TEST_F(ValueFormatTest, FormatInt8)
{
    EXPECT_EQ(formatValue(int8_t(127)), "127");
    EXPECT_EQ(formatValue(int8_t(-128)), "-128");
}

TEST_F(ValueFormatTest, FormatUint16)
{
    EXPECT_EQ(formatValue(uint16_t(1234)), "1234");
}

TEST_F(ValueFormatTest, FormatString)
{
    EXPECT_EQ(formatValue(std::string("hello")), "hello");
    EXPECT_EQ(formatValue(std::string("world")), "world");
}

TEST_F(ValueFormatTest, FormatStringView)
{
    std::string_view sv = "test";
    EXPECT_EQ(formatValue(sv), "test");
}

TEST_F(ValueFormatTest, FormatIPv4Address)
{
    types::IPv4Address addr(0xC0A80101);
    EXPECT_EQ(formatValue(addr), "192.168.1.1");
}

TEST_F(ValueFormatTest, FormatIPv4AddressZero)
{
    types::IPv4Address addr(uint32_t{0});
    EXPECT_EQ(formatValue(addr), "0.0.0.0");
}

TEST_F(ValueFormatTest, FormatIPv4AddressMax)
{
    types::IPv4Address addr(~uint32_t{0});
    EXPECT_EQ(formatValue(addr), "255.255.255.255");
}

TEST_F(ValueFormatTest, FormatIPv4Prefix)
{
    types::IPv4Prefix prefix(0x0A000000, 8);
    EXPECT_EQ(formatValue(prefix), "10.0.0.0/8");
}

TEST_F(ValueFormatTest, FormatIPv4PrefixSlash32)
{
    types::IPv4Prefix prefix(0xC0A80101, 32);
    EXPECT_EQ(formatValue(prefix), "192.168.1.1/32");
}

TEST_F(ValueFormatTest, FormatIPv6Address)
{
    types::IPv6Address addr;
    addr.addr = (static_cast<__uint128_t>(0x20010db800000000) << 64) | 0000000000000001ULL;
    std::string result = formatValue(addr);
    EXPECT_NE(result.find("2001:db8"), std::string::npos);
    EXPECT_NE(result.find(":0001"), std::string::npos);
}

TEST_F(ValueFormatTest, FormatIPv6Prefix)
{
    types::IPAddress addr;
    addr.raw = static_cast<__uint128_t>(0x20010db800000000) << 64;
    types::IPv6Prefix prefix(addr, 32);
    std::string result = formatValue(prefix);
    EXPECT_NE(result.find("/32"), std::string::npos);
}

// ============================================================================
// CommandKey Tests
// ============================================================================

class CommandKeyTest : public ::testing::Test {};

TEST_F(CommandKeyTest, DefaultConstruction)
{
    CommandKey key;
    EXPECT_EQ(key.configId, ~0u);
    EXPECT_EQ(key.tupleMember, 0xFFFFu);
    EXPECT_EQ(key.enumMember, 0xFFFFu);
}

TEST_F(CommandKeyTest, EqualityPlainKey)
{
    CommandKey k1{123, 0xFFFFu, 0xFFFFu};
    CommandKey k2{123, 0xFFFFu, 0xFFFFu};
    EXPECT_EQ(k1, k2);
}

TEST_F(CommandKeyTest, InequalityConfigId)
{
    CommandKey k1{123, 0xFFFFu, 0xFFFFu};
    CommandKey k2{456, 0xFFFFu, 0xFFFFu};
    EXPECT_NE(k1, k2);
}

TEST_F(CommandKeyTest, InequalityTupleMember)
{
    CommandKey k1{123, 0, 0xFFFFu};
    CommandKey k2{123, 1, 0xFFFFu};
    EXPECT_NE(k1, k2);
}

TEST_F(CommandKeyTest, InequalityEnumMember)
{
    CommandKey k1{123, 0xFFFFu, 0};
    CommandKey k2{123, 0xFFFFu, 1};
    EXPECT_NE(k1, k2);
}

TEST_F(CommandKeyTest, TupleKey)
{
    CommandKey key{100, 5, 0xFFFFu};
    EXPECT_EQ(key.configId, 100);
    EXPECT_EQ(key.tupleMember, 5);
    EXPECT_EQ(key.enumMember, 0xFFFFu);
}

TEST_F(CommandKeyTest, EnumKey)
{
    CommandKey key{100, 0xFFFFu, 3};
    EXPECT_EQ(key.configId, 100);
    EXPECT_EQ(key.tupleMember, 0xFFFFu);
    EXPECT_EQ(key.enumMember, 3);
}

// ============================================================================
// CommandKeyHash Tests
// ============================================================================

class CommandKeyHashTest : public ::testing::Test
{
};

TEST_F(CommandKeyHashTest, HashConsistency)
{
    CommandKey k1{123, 0xFFFFu, 0xFFFFu};
    CommandKey k2{123, 0xFFFFu, 0xFFFFu};

    CommandKeyHash h;
    EXPECT_EQ(h(k1), h(k2));
}

TEST_F(CommandKeyHashTest, HashDifferentForDifferentConfigId)
{
    CommandKey k1{123, 0xFFFFu, 0xFFFFu};
    CommandKey k2{456, 0xFFFFu, 0xFFFFu};

    CommandKeyHash h;
    EXPECT_NE(h(k1), h(k2));
}

TEST_F(CommandKeyHashTest, HashDifferentForDifferentTuple)
{
    CommandKey k1{123, 0, 0xFFFFu};
    CommandKey k2{123, 1, 0xFFFFu};

    CommandKeyHash h;
    EXPECT_NE(h(k1), h(k2));
}

TEST_F(CommandKeyHashTest, HashDifferentForDifferentEnum)
{
    CommandKey k1{123, 0xFFFFu, 0};
    CommandKey k2{123, 0xFFFFu, 1};

    CommandKeyHash h;
    EXPECT_NE(h(k1), h(k2));
}

// ============================================================================
// CommandWords Tests
// ============================================================================

class CommandWordsTest : public ::testing::Test
{
};

TEST_F(CommandWordsTest, EmptyWords)
{
    CommandWords words;
    EXPECT_TRUE(words.words.empty());
}

TEST_F(CommandWordsTest, SingleWord)
{
    CommandWords words;
    words.words.push_back("router");

    EXPECT_EQ(words.words.size(), 1);
    EXPECT_EQ(words.words[0], "router");
}

TEST_F(CommandWordsTest, MultipleWords)
{
    CommandWords words;
    words.words.push_back("router");
    words.words.push_back("ospf");
    words.words.push_back("1");

    EXPECT_EQ(words.words.size(), 3);
    EXPECT_EQ(words.words[0], "router");
    EXPECT_EQ(words.words[1], "ospf");
    EXPECT_EQ(words.words[2], "1");
}

// ============================================================================
// ConfigSerializer Tests (Mock-based)
// ============================================================================

class MockCommandPathIndex : public CommandPathIndex
{
public:
    MOCK_METHOD(CommandWords, lookup, (uint32_t), (const));
    MOCK_METHOD(CommandWords, lookupTupleMember, (uint32_t, uint16_t), (const));
    MOCK_METHOD(CommandWords, lookupEnumMember, (uint32_t, uint16_t), (const));
};

class MockConfigWriter : public ConfigWriter
{
public:
    MockConfigWriter() : ConfigWriter(1) {}
    MOCK_METHOD(void, line, (std::string_view), (override));
    MOCK_METHOD(void, line, (const std::vector<std::string>&), (override));
    MOCK_METHOD(void, blank, (), (override));
};

class ConfigSerializerTest : public ::testing::Test
{
protected:
    MockCommandPathIndex mockIndex;
    MockConfigWriter mockWriter;

    ConfigSerializer serializer{mockIndex, mockWriter};
};

// ============================================================================
// Integration-style Tests
// ============================================================================

class ConfigSerializerIntegrationTest : public ::testing::Test
{
protected:
    CommandPathIndex pathIndex;
    ConfigWriter writer;
    ConfigSerializer serializer{pathIndex, writer};

    void SetUp() override
    {
        writer.str();
    }
};

TEST_F(ConfigSerializerIntegrationTest, WriterBasicOutput)
{
    std::vector<std::string> line = {"ip", "address", "10.0.0.1"};
    writer.line(line);

    EXPECT_EQ(writer.str(), "ip address 10.0.0.1\n");
}

TEST_F(ConfigSerializerIntegrationTest, WriterNestedConfig)
{
    writer.line("router ospf 1");
    {
        ConfigWriter::Scope s = writer.scope();
        std::vector<std::string> line = {"area", "0", "range", "10.0.0.0", "255.255.255.0"};
        writer.line(line);
    }

    std::string expected = "router ospf 1\n area 0 range 10.0.0.0 255.255.255.0\n";
    EXPECT_EQ(writer.str(), expected);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

class EdgeCaseTests : public ::testing::Test
{
};

TEST_F(EdgeCaseTests, EmptyCommandWords)
{
    CommandWords words;
    words.words.clear();
    EXPECT_TRUE(words.words.empty());
}

TEST_F(EdgeCaseTests, VeryLongWord)
{
    std::string longWord(1000, 'a');
    EXPECT_EQ(formatValue(longWord), longWord);
}

TEST_F(EdgeCaseTests, SpecialCharactersInString)
{
    std::string special = "hello@world#test";
    EXPECT_EQ(formatValue(special), special);
}

TEST_F(EdgeCaseTests, NumericEdges)
{
    EXPECT_EQ(formatValue(uint32_t(0)), "0");
    EXPECT_EQ(formatValue(uint32_t(1)), "1");
    EXPECT_EQ(formatValue(uint32_t(~0u)), std::to_string(~0u));
}

TEST_F(EdgeCaseTests, WriterLargeIndentDepth)
{
    ConfigWriter writer;
    for (int i = 0; i < 10; ++i)
        writer.push();

    writer.line("deeply nested");

    std::string expected(10, ' ');
    expected += "deeply nested\n";
    EXPECT_EQ(writer.str(), expected);
}

TEST_F(EdgeCaseTests, WriterManyLines)
{
    ConfigWriter writer;
    for (int i = 0; i < 100; ++i)
        writer.line("line " + std::to_string(i));

    std::string result = writer.str();
    EXPECT_NE(result.find("line 0"), std::string::npos);
    EXPECT_NE(result.find("line 99"), std::string::npos);
}

TEST_F(EdgeCaseTests, KeyHashUniformDistribution)
{
    CommandKeyHash h;
    std::set<size_t> hashes;

    for (uint32_t id = 0; id < 100; ++id)
    {
        CommandKey k{id, 0xFFFFu, 0xFFFFu};
        hashes.insert(h(k));
    }

    EXPECT_GT(hashes.size(), 90);
}

// ============================================================================
// Stress Tests
// ============================================================================

class StressTests : public ::testing::Test
{
};

TEST_F(StressTests, ConfigWriterLargeOutput)
{
    ConfigWriter writer;

    for (int i = 0; i < 1000; ++i)
    {
        std::vector<std::string> line = {"command", std::to_string(i)};
        writer.line(line);
    }

    std::string result = writer.str();
    EXPECT_GT(result.size(), 10000);
}

TEST_F(StressTests, DeepNestedScopes)
{
    ConfigWriter writer;
    std::vector<ConfigWriter::Scope> scopes;

    for (int i = 0; i < 50; ++i)
    {
        writer.line("level " + std::to_string(i));
        scopes.emplace_back(writer.scope());
    }

    writer.line("deepest");

    std::string result = writer.str();
    EXPECT_NE(result.find("deepest"), std::string::npos);
}

TEST_F(StressTests, ManyCommandKeys)
{
    std::unordered_map<CommandKey, uint32_t, CommandKeyHash> map;

    for (uint32_t id = 0; id < 10000; ++id)
    {
        CommandKey k{id, 0xFFFFu, 0xFFFFu};
        map[k] = id;
    }

    EXPECT_EQ(map.size(), 10000);

    for (uint32_t id = 0; id < 10000; ++id)
    {
        CommandKey k{id, 0xFFFFu, 0xFFFFu};
        EXPECT_EQ(map[k], id);
    }
}

// ============================================================================
// Formatter Type Constraints Tests
// ============================================================================

class FormatterConstraintsTest : public ::testing::Test
{
};

TEST_F(FormatterConstraintsTest, BoolFormatNotAllowed)
{
    EXPECT_FALSE(requires(const bool& v) { formatValue(v); });
}

TEST_F(FormatterConstraintsTest, IntegralTypes)
{
    EXPECT_TRUE(requires(uint32_t v) { formatValue(v); });
    EXPECT_TRUE(requires(int32_t v) { formatValue(v); });
    EXPECT_TRUE(requires(uint16_t v) { formatValue(v); });
    EXPECT_TRUE(requires(int16_t v) { formatValue(v); });
}

TEST_F(FormatterConstraintsTest, StringTypes)
{
    EXPECT_TRUE(requires(const std::string& v) { formatValue(v); });
    EXPECT_TRUE(requires(std::string_view v) { formatValue(v); });
}

// ============================================================================
// Parser/Serialization Round-trip Tests (conceptual)
// ============================================================================

class RoundTripTest : public ::testing::Test
{
};

TEST_F(RoundTripTest, SimpleConfigLine)
{
    ConfigWriter writer;
    std::vector<std::string> config = {"router", "ospf", "1"};
    writer.line(config);

    EXPECT_EQ(writer.str(), "router ospf 1\n");
}

TEST_F(RoundTripTest, ConfigWithIPv4)
{
    ConfigWriter writer;
    types::IPv4Address addr(192, 168, 1, 1);

    std::vector<std::string> line = {"interface", "Gi0/0"};
    writer.line(line);
    {
        ConfigWriter::Scope s = writer.scope();
        line.clear();
        line = {"ip", "address", formatValue(addr)};
        writer.line(line);
    }

    std::string result = writer.str();
    EXPECT_NE(result.find("192.168.1.1"), std::string::npos);
}

TEST_F(RoundTripTest, ConfigWithPrefix)
{
    ConfigWriter writer;
    types::IPv4Prefix prefix(types::IPv4Address(10, 0, 0, 0), 8);

    std::vector<std::string> line = {"network", formatValue(prefix)};
    writer.line(line);

    EXPECT_EQ(writer.str(), "network 10.0.0.0/8\n");
}

// ============================================================================
// Registry-based Serialization Tests
// ============================================================================

// Mock field accessor for testing scalar values
template <typename T>
class MockScalarAccessor
{
public:
    using Field = typename std::remove_cvref_t<T>;

    explicit MockScalarAccessor(const T& val, bool overridden = true)
        : value(val), is_overridden(overridden)
    {}

    const T& load() const { return value; }
    bool overridden() const { return is_overridden; }

private:
    T value;
    bool is_overridden;
};

// Mock container accessor for testing child registries
class MockContainerRegistry
{
public:
    using type = uint32_t;

    MockContainerRegistry() = default;

    template <typename Callback>
    void visit(size_t /*idx*/, Callback&& /*cb*/)
    {
    }
};

// Simulated field enum for testing
enum class TestFieldEnum : uint16_t
{
    ENABLED = 0,
    INTERFACE_NAME = 1,
    IP_ADDRESS = 2,
    PRIORITY = 3,
    COUNT
};

class RegistrySerializationTest : public ::testing::Test
{
protected:
    CommandPathIndex pathIndex;
    ConfigWriter writer;
    ConfigSerializer serializer{pathIndex, writer};

    // Helper to create command words for testing
    CommandWords createCommandWords(const std::vector<std::string_view>& words_list)
    {
        CommandWords words;
        words.words.assign(words_list.begin(), words_list.end());
        return words;
    }
};

TEST_F(RegistrySerializationTest, SerializeScalarBoolTrue)
{
    ConfigWriter writer;
    MockScalarAccessor<bool> accessor(true, true);

    std::vector<std::string> line = {"shutdown"};
    writer.line(line);

    std::string result = writer.str();
    EXPECT_EQ(result, "shutdown\n");
}

TEST_F(RegistrySerializationTest, SerializeScalarBoolFalse)
{
    ConfigWriter writer;
    MockScalarAccessor<bool> accessor(false, true);

    std::vector<std::string> line = {"no", "shutdown"};
    writer.line(line);

    std::string result = writer.str();
    EXPECT_EQ(result, "no shutdown\n");
}

TEST_F(RegistrySerializationTest, SerializeScalarUint32)
{
    ConfigWriter writer;
    uint32_t value = 65000;
    MockScalarAccessor<uint32_t> accessor(value, true);

    std::vector<std::string> line = {"router", "bgp", formatValue(value)};
    writer.line(line);

    std::string result = writer.str();
    EXPECT_NE(result.find("65000"), std::string::npos);
}

TEST_F(RegistrySerializationTest, SerializeScalarIPv4Address)
{
    ConfigWriter writer;
    types::IPv4Address addr(192, 168, 1, 1);
    MockScalarAccessor<types::IPv4Address> accessor(addr, true);

    std::vector<std::string> line = {"ip", "address", formatValue(addr)};
    writer.line(line);

    std::string result = writer.str();
    EXPECT_EQ(result, "ip address 192.168.1.1\n");
}

TEST_F(RegistrySerializationTest, SerializeScalarIPv4Prefix)
{
    ConfigWriter writer;
    types::IPv4Prefix prefix(types::IPv4Address(10, 1, 0, 0), 16);
    MockScalarAccessor<types::IPv4Prefix> accessor(prefix, true);

    std::vector<std::string> line = {"network", formatValue(prefix)};
    writer.line(line);

    std::string result = writer.str();
    EXPECT_EQ(result, "network 10.1.0.0/16\n");
}

TEST_F(RegistrySerializationTest, SerializeScalarString)
{
    ConfigWriter writer;
    std::string hostname = "router1";
    MockScalarAccessor<std::string> accessor(hostname, true);

    std::vector<std::string> line = {"hostname", formatValue(hostname)};
    writer.line(line);

    std::string result = writer.str();
    EXPECT_EQ(result, "hostname router1\n");
}

TEST_F(RegistrySerializationTest, SerializeNotOverriddenField)
{
    ConfigWriter writer;
    MockScalarAccessor<uint32_t> accessor(100, false);

    // Should not output anything for non-overridden fields
    if (accessor.overridden())
    {
        std::vector<std::string> line = {"value", formatValue(accessor.load())};
        writer.line(line);
    }

    EXPECT_EQ(writer.str(), "");
}

TEST_F(RegistrySerializationTest, SerializeMultipleScalarFields)
{
    ConfigWriter writer;

    // Simulate multiple fields being serialized
    std::vector<std::string> line1 = {"router", "ospf", "1"};
    writer.line(line1);

    {
        ConfigWriter::Scope s = writer.scope();
        std::vector<std::string> line2 = {"router-id", formatValue(types::IPv4Address(10, 0, 0, 1))};
        writer.line(line2);

        std::vector<std::string> line3 = {"network", formatValue(types::IPv4Prefix(10, 0, 0, 0, 8))};
        writer.line(line3);
    }

    std::string result = writer.str();
    EXPECT_NE(result.find("router ospf 1"), std::string::npos);
    EXPECT_NE(result.find("router-id 10.0.0.1"), std::string::npos);
}

TEST_F(RegistrySerializationTest, SerializeNestedOwnedList)
{
    ConfigWriter writer;

    // Simulate owned-list serialization (e.g., router ospf -> areas)
    std::vector<std::string> line1 = {"router", "ospf", "1"};
    writer.line(line1);

    {
        ConfigWriter::Scope scope = writer.scope();

        // First area
        std::vector<std::string> area1 = {"area", "0"};
        writer.line(area1);
        {
            ConfigWriter::Scope area_scope = writer.scope();
            std::vector<std::string> range = {"range", formatValue(types::IPv4Prefix(10, 0, 0, 0, 8))};
            writer.line(range);
        }

        // Second area
        std::vector<std::string> area2 = {"area", "1"};
        writer.line(area2);
        {
            ConfigWriter::Scope area_scope = writer.scope();
            std::vector<std::string> range2 = {"range", formatValue(types::IPv4Prefix(10, 1, 0, 0, 16))};
            writer.line(range2);
        }
    }

    std::string result = writer.str();
    EXPECT_NE(result.find("router ospf 1"), std::string::npos);
    EXPECT_NE(result.find("area 0"), std::string::npos);
    EXPECT_NE(result.find("area 1"), std::string::npos);
    EXPECT_NE(result.find("10.0.0.0/8"), std::string::npos);
    EXPECT_NE(result.find("10.1.0.0/16"), std::string::npos);
}

TEST_F(RegistrySerializationTest, SerializeEnumField)
{
    ConfigWriter writer;

    // Simulate enum field serialization where the leaf keyword is the value
    std::vector<std::string> line = {"area", "0", "type", "stub"};
    writer.line(line);

    std::string result = writer.str();
    EXPECT_EQ(result, "area 0 type stub\n");
}

TEST_F(RegistrySerializationTest, SerializeEnumBitMap)
{
    ConfigWriter writer;

    // Simulate bitmap field serialization (multiple flags on one line)
    std::vector<std::string> line = {"ip", "access-list", "route-map", "permit", "reject"};
    writer.line(line);

    std::string result = writer.str();
    EXPECT_NE(result.find("permit"), std::string::npos);
    EXPECT_NE(result.find("reject"), std::string::npos);
}

TEST_F(RegistrySerializationTest, SerializeBGPConfiguration)
{
    ConfigWriter writer;

    // Complete BGP config example
    std::vector<std::string> bgp = {"router", "bgp", "65000"};
    writer.line(bgp);

    {
        ConfigWriter::Scope bgp_scope = writer.scope();

        std::vector<std::string> rid = {"bgp", "router-id", formatValue(types::IPv4Address(1, 1, 1, 1))};
        writer.line(rid);

        std::vector<std::string> neighbor = {"neighbor", formatValue(types::IPv4Address(192, 168, 1, 1))};
        writer.line(neighbor);

        {
            ConfigWriter::Scope neighbor_scope = writer.scope();
            std::vector<std::string> remote_as = {"remote-as", "65001"};
            writer.line(remote_as);
        }
    }

    std::string result = writer.str();
    EXPECT_NE(result.find("router bgp 65000"), std::string::npos);
    EXPECT_NE(result.find("bgp router-id 1.1.1.1"), std::string::npos);
    EXPECT_NE(result.find("neighbor 192.168.1.1"), std::string::npos);
}

TEST_F(RegistrySerializationTest, SerializeOSPFConfiguration)
{
    ConfigWriter writer;

    // Complete OSPF config example
    std::vector<std::string> ospf = {"router", "ospf", "1"};
    writer.line(ospf);

    {
        ConfigWriter::Scope ospf_scope = writer.scope();

        std::vector<std::string> rid = {"router-id", formatValue(types::IPv4Address(2, 2, 2, 2))};
        writer.line(rid);

        std::vector<std::string> area = {"area", "0"};
        writer.line(area);

        {
            ConfigWriter::Scope area_scope = writer.scope();
            std::vector<std::string> range = {"range", formatValue(types::IPv4Prefix(192, 168, 0, 0, 16))};
            writer.line(range);

            std::vector<std::string> network = {"network", formatValue(types::IPv4Prefix(10, 0, 0, 0, 8)), "area", "0"};
            writer.line(network);
        }
    }

    std::string result = writer.str();
    EXPECT_NE(result.find("router ospf 1"), std::string::npos);
    EXPECT_NE(result.find("router-id 2.2.2.2"), std::string::npos);
    EXPECT_NE(result.find("area 0"), std::string::npos);
}

TEST_F(RegistrySerializationTest, SerializeInterfaceConfiguration)
{
    ConfigWriter writer;

    // Complete interface config example
    std::vector<std::string> iface = {"interface", "GigabitEthernet", "0/0"};
    writer.line(iface);

    {
        ConfigWriter::Scope iface_scope = writer.scope();

        std::vector<std::string> desc = {"description", "WAN link to ISP"};
        writer.line(desc);

        std::vector<std::string> ip = {"ip", "address", formatValue(types::IPv4Address(203, 0, 113, 1)), formatValue(types::IPv4Address(255, 255, 255, 0))};
        writer.line(ip);

        std::vector<std::string> no_shut = {"no", "shutdown"};
        writer.line(no_shut);
    }

    std::string result = writer.str();
    EXPECT_NE(result.find("interface GigabitEthernet 0/0"), std::string::npos);
    EXPECT_NE(result.find("description WAN link to ISP"), std::string::npos);
    EXPECT_NE(result.find("203.0.113.1"), std::string::npos);
}

TEST_F(RegistrySerializationTest, SerializeWithDifferentIndentWidths)
{
    // Test with 2-space indent
    {
        ConfigWriter writer(2);
        std::vector<std::string> line1 = {"router", "bgp", "65000"};
        writer.line(line1);

        {
            ConfigWriter::Scope s = writer.scope();
            std::vector<std::string> line2 = {"bgp", "router-id", "1.1.1.1"};
            writer.line(line2);
        }

        std::string result = writer.str();
        EXPECT_NE(result.find("  bgp router-id"), std::string::npos);
    }

    // Test with 4-space indent
    {
        ConfigWriter writer(4);
        std::vector<std::string> line1 = {"router", "ospf", "1"};
        writer.line(line1);

        {
            ConfigWriter::Scope s = writer.scope();
            std::vector<std::string> line2 = {"router-id", "2.2.2.2"};
            writer.line(line2);
        }

        std::string result = writer.str();
        EXPECT_NE(result.find("    router-id"), std::string::npos);
    }
}

TEST_F(RegistrySerializationTest, SerializeEmptyRegistry)
{
    ConfigWriter writer;
    // No fields to serialize
    EXPECT_EQ(writer.str(), "");
}

TEST_F(RegistrySerializationTest, SerializeAllFieldTypes)
{
    ConfigWriter writer;

    // Simulate a registry with all field types
    writer.line("router ospf 1");
    {
        ConfigWriter::Scope s = writer.scope();

        // Bool field
        writer.line("no shutdown");

        // Uint32 field
        writer.line("default-cost 100");

        // String field
        writer.line("description Core OSPF Process");

        // IPv4Address field
        writer.line("router-id 192.168.255.1");

        // IPv4Prefix field
        writer.line("summary-address 10.0.0.0 255.255.255.0");

        // Nested owned-list (area)
        writer.line("area 0");
        {
            ConfigWriter::Scope area_scope = writer.scope();
            writer.line("range 10.0.0.0 255.255.255.0");
        }
    }

    std::string result = writer.str();
    EXPECT_GT(result.size(), 50);
    EXPECT_NE(result.find("router ospf"), std::string::npos);
    EXPECT_NE(result.find("shutdown"), std::string::npos);
    EXPECT_NE(result.find("192.168.255.1"), std::string::npos);
}

TEST_F(RegistrySerializationTest, SerializeComplexHierarchy)
{
    ConfigWriter writer;

    // Multi-level hierarchy
    writer.line("router bgp 65000");
    {
        ConfigWriter::Scope bgp = writer.scope();
        writer.line("bgp router-id 1.1.1.1");

        // VRF (owned-list)
        writer.line("address-family ipv4 vrf CUSTOMER1");
        {
            ConfigWriter::Scope vrf = writer.scope();
            writer.line("network 10.0.0.0 255.0.0.0");
            writer.line("redistribute ospf 1");

            // Neighbor under VRF
            writer.line("neighbor 192.168.1.1");
            {
                ConfigWriter::Scope neighbor = writer.scope();
                writer.line("remote-as 65001");
                writer.line("route-map IN in");
            }
        }

        // Another VRF
        writer.line("address-family ipv4 vrf CUSTOMER2");
        {
            ConfigWriter::Scope vrf2 = writer.scope();
            writer.line("network 172.16.0.0 255.255.0.0");
        }
    }

    std::string result = writer.str();
    size_t bgp_pos = result.find("router bgp 65000");
    size_t vrf1_pos = result.find("CUSTOMER1");
    size_t vrf2_pos = result.find("CUSTOMER2");

    EXPECT_NE(bgp_pos, std::string::npos);
    EXPECT_NE(vrf1_pos, std::string::npos);
    EXPECT_NE(vrf2_pos, std::string::npos);
    EXPECT_LT(bgp_pos, vrf1_pos);
    EXPECT_LT(vrf1_pos, vrf2_pos);
}
#endif
