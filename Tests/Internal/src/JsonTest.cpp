#include <gtest/gtest.h>
#include <utils/Json.hpp>
#include <cstdio>
#include <fstream>
#include <string>

using utils::json::JsonNode;
using utils::json::JsonParser;

namespace
{
JsonNode parse(std::string_view text)
{
    return utils::json::parse(text);
}
}

class Internal_JsonTest : public ::testing::Test {};

// SCALARS

TEST_F(Internal_JsonTest, ParsesStringLiteral)
{
    JsonNode n = parse(R"("hello")");
    EXPECT_TRUE(n.isString());
    EXPECT_EQ(n.asString(), "hello");
}

TEST_F(Internal_JsonTest, ParsesEmptyString)
{
    JsonNode n = parse(R"("")");
    EXPECT_TRUE(n.isString());
    EXPECT_EQ(n.asString(), "");
}

TEST_F(Internal_JsonTest, ParsesIntegerNumber)
{
    JsonNode n = parse("42");
    EXPECT_TRUE(n.isNumber());
    EXPECT_DOUBLE_EQ(n.asNumber(), 42.0);
}

TEST_F(Internal_JsonTest, ParsesNegativeNumber)
{
    EXPECT_DOUBLE_EQ(parse("-17").asNumber(), -17.0);
}

TEST_F(Internal_JsonTest, ParsesFractionalNumber)
{
    EXPECT_DOUBLE_EQ(parse("3.25").asNumber(), 3.25);
}

TEST_F(Internal_JsonTest, ParsesExponentNumber)
{
    EXPECT_DOUBLE_EQ(parse("1e3").asNumber(), 1000.0);
    EXPECT_DOUBLE_EQ(parse("1E3").asNumber(), 1000.0);
    EXPECT_DOUBLE_EQ(parse("2.5e-2").asNumber(), 0.025);
    EXPECT_DOUBLE_EQ(parse("2.5e+2").asNumber(), 250.0);
}

TEST_F(Internal_JsonTest, ParsesTrue)
{
    JsonNode n = parse("true");
    EXPECT_TRUE(n.isBool());
    EXPECT_TRUE(n.asBool());
}

TEST_F(Internal_JsonTest, ParsesFalse)
{
    JsonNode n = parse("false");
    EXPECT_TRUE(n.isBool());
    EXPECT_FALSE(n.asBool());
}

TEST_F(Internal_JsonTest, ParsesNull)
{
    JsonNode n = parse("null");
    EXPECT_TRUE(n.isNull());
    EXPECT_EQ(n.type, JsonNode::NILL);
}

// A default-constructed node is null, so untouched fields never read as a value.
TEST_F(Internal_JsonTest, DefaultNodeIsNull)
{
    JsonNode n;
    EXPECT_TRUE(n.isNull());
    EXPECT_EQ(n.size(), 0u);
}

// STRING ESCAPES

TEST_F(Internal_JsonTest, ParsesSimpleEscapes)
{
    JsonNode n = parse(R"("a\"b\\c\/d\be\ff\ng\rh\ti")");
    EXPECT_EQ(n.asString(), "a\"b\\c/d\be\ff\ng\rh\ti");
}

TEST_F(Internal_JsonTest, ParsesUnicodeEscapeAscii)
{
    EXPECT_EQ(parse(R"("A")").asString(), "A");
}

TEST_F(Internal_JsonTest, ParsesUnicodeEscapeTwoByte)
{
    // U+00E9 LATIN SMALL LETTER E WITH ACUTE
    EXPECT_EQ(parse(R"("é")").asString(), "\xC3\xA9");
}

TEST_F(Internal_JsonTest, ParsesUnicodeEscapeThreeByte)
{
    // U+20AC EURO SIGN
    EXPECT_EQ(parse(R"("€")").asString(), "\xE2\x82\xAC");
}

TEST_F(Internal_JsonTest, ParsesSurrogatePair)
{
    // U+1D11E MUSICAL SYMBOL G CLEF
    EXPECT_EQ(parse(R"("𝄞")").asString(), "\xF0\x9D\x84\x9E");
}

TEST_F(Internal_JsonTest, RejectsBadEscape)
{
    EXPECT_THROW(parse(R"("\q")"), std::runtime_error);
}

TEST_F(Internal_JsonTest, RejectsTruncatedUnicodeEscape)
{
    EXPECT_THROW(parse(R"("\u00")"), std::runtime_error);
}

TEST_F(Internal_JsonTest, RejectsBadHexInUnicodeEscape)
{
    EXPECT_THROW(parse(R"("\u00zz")"), std::runtime_error);
}

TEST_F(Internal_JsonTest, RejectsUnterminatedString)
{
    EXPECT_THROW(parse(R"("abc)"), std::runtime_error);
}

// ARRAYS

TEST_F(Internal_JsonTest, ParsesEmptyArray)
{
    JsonNode n = parse("[]");
    EXPECT_TRUE(n.isArray());
    EXPECT_EQ(n.size(), 0u);
}

TEST_F(Internal_JsonTest, ParsesFlatArray)
{
    JsonNode n = parse("[1, 2, 3]");
    ASSERT_EQ(n.size(), 3u);
    EXPECT_DOUBLE_EQ(n.at(0).asNumber(), 1.0);
    EXPECT_DOUBLE_EQ(n.at(1).asNumber(), 2.0);
    EXPECT_DOUBLE_EQ(n.at(2).asNumber(), 3.0);
}

TEST_F(Internal_JsonTest, ParsesMixedArray)
{
    JsonNode n = parse(R"([1, "two", true, null, [], {}])");
    ASSERT_EQ(n.size(), 6u);
    EXPECT_TRUE(n.at(0).isNumber());
    EXPECT_TRUE(n.at(1).isString());
    EXPECT_TRUE(n.at(2).isBool());
    EXPECT_TRUE(n.at(3).isNull());
    EXPECT_TRUE(n.at(4).isArray());
    EXPECT_TRUE(n.at(5).isObject());
}

TEST_F(Internal_JsonTest, ParsesNestedArrays)
{
    JsonNode n = parse("[[1, [2]], [3]]");
    ASSERT_EQ(n.size(), 2u);
    ASSERT_EQ(n.at(0).size(), 2u);
    EXPECT_DOUBLE_EQ(n.at(0).at(0).asNumber(), 1.0);
    EXPECT_DOUBLE_EQ(n.at(0).at(1).at(0).asNumber(), 2.0);
    EXPECT_DOUBLE_EQ(n.at(1).at(0).asNumber(), 3.0);
}

TEST_F(Internal_JsonTest, ArrayElementsHaveNoName)
{
    JsonNode n = parse(R"(["a", "b"])");
    EXPECT_TRUE(n.at(0).name.empty());
    EXPECT_TRUE(n.at(1).name.empty());
}

TEST_F(Internal_JsonTest, RejectsUnclosedArray)
{
    EXPECT_THROW(parse("[1, 2"), std::runtime_error);
}

TEST_F(Internal_JsonTest, RejectsMissingArraySeparator)
{
    EXPECT_THROW(parse("[1 2]"), std::runtime_error);
}

// OBJECTS

TEST_F(Internal_JsonTest, ParsesEmptyObject)
{
    JsonNode n = parse("{}");
    EXPECT_TRUE(n.isObject());
    EXPECT_EQ(n.size(), 0u);
}

TEST_F(Internal_JsonTest, ParsesFlatObject)
{
    JsonNode n = parse(R"({"a": 1, "b": "two", "c": false})");
    ASSERT_EQ(n.size(), 3u);
    EXPECT_DOUBLE_EQ(n["a"].asNumber(), 1.0);
    EXPECT_EQ(n["b"].asString(), "two");
    EXPECT_FALSE(n["c"].asBool());
}

TEST_F(Internal_JsonTest, MembersCarryTheirKeyAsName)
{
    JsonNode n = parse(R"({"key": 1})");
    EXPECT_EQ(n.at(0).name, "key");
}

TEST_F(Internal_JsonTest, ParsesNestedObjects)
{
    JsonNode n = parse(R"({"outer": {"inner": {"leaf": 7}}})");
    EXPECT_DOUBLE_EQ(n["outer"]["inner"]["leaf"].asNumber(), 7.0);
}

TEST_F(Internal_JsonTest, ObjectPreservesInsertionOrder)
{
    JsonNode n = parse(R"({"z": 1, "a": 2, "m": 3})");
    ASSERT_EQ(n.size(), 3u);
    EXPECT_EQ(n.at(0).name, "z");
    EXPECT_EQ(n.at(1).name, "a");
    EXPECT_EQ(n.at(2).name, "m");
}

TEST_F(Internal_JsonTest, ContainsFindsPresentAndAbsentKeys)
{
    JsonNode n = parse(R"({"present": 1})");
    EXPECT_TRUE(n.contains("present"));
    EXPECT_FALSE(n.contains("absent"));
}

TEST_F(Internal_JsonTest, FindReturnsNullForMissingKey)
{
    JsonNode n = parse(R"({"a": 1})");
    ASSERT_NE(n.find("a"), nullptr);
    EXPECT_DOUBLE_EQ(n.find("a")->asNumber(), 1.0);
    EXPECT_EQ(n.find("nope"), nullptr);
}

TEST_F(Internal_JsonTest, FindOnNonObjectReturnsNull)
{
    EXPECT_EQ(parse("[1, 2]").find("a"), nullptr);
    EXPECT_EQ(parse("42").find("a"), nullptr);
}

TEST_F(Internal_JsonTest, SubscriptThrowsOnMissingKey)
{
    JsonNode n = parse(R"({"a": 1})");
    EXPECT_THROW(n["missing"], std::out_of_range);
}

TEST_F(Internal_JsonTest, RejectsUnquotedKey)
{
    EXPECT_THROW(parse("{a: 1}"), std::runtime_error);
}

TEST_F(Internal_JsonTest, RejectsMissingColon)
{
    EXPECT_THROW(parse(R"({"a" 1})"), std::runtime_error);
}

TEST_F(Internal_JsonTest, RejectsUnclosedObject)
{
    EXPECT_THROW(parse(R"({"a": 1)"), std::runtime_error);
}

TEST_F(Internal_JsonTest, RejectsMissingObjectSeparator)
{
    EXPECT_THROW(parse(R"({"a": 1 "b": 2})"), std::runtime_error);
}

// TYPE ACCESS ERRORS

TEST_F(Internal_JsonTest, AsStringThrowsOnNonString)
{
    EXPECT_THROW(parse("1").asString(), std::runtime_error);
    EXPECT_THROW(parse("true").asString(), std::runtime_error);
    EXPECT_THROW(parse("null").asString(), std::runtime_error);
}

TEST_F(Internal_JsonTest, AsNumberThrowsOnNonNumber)
{
    EXPECT_THROW(parse(R"("s")").asNumber(), std::runtime_error);
    EXPECT_THROW(parse("false").asNumber(), std::runtime_error);
}

TEST_F(Internal_JsonTest, AsBoolThrowsOnNonBool)
{
    EXPECT_THROW(parse("1").asBool(), std::runtime_error);
    EXPECT_THROW(parse(R"("true")").asBool(), std::runtime_error);
}

TEST_F(Internal_JsonTest, SizeIsZeroForScalars)
{
    EXPECT_EQ(parse("1").size(), 0u);
    EXPECT_EQ(parse(R"("abc")").size(), 0u);
    EXPECT_EQ(parse("true").size(), 0u);
    EXPECT_EQ(parse("null").size(), 0u);
}

TEST_F(Internal_JsonTest, AtThrowsOnScalar)
{
    EXPECT_THROW(parse("1").at(0), std::runtime_error);
}

TEST_F(Internal_JsonTest, AtThrowsWhenOutOfRange)
{
    EXPECT_THROW(parse("[1]").at(1), std::out_of_range);
    EXPECT_THROW(parse(R"({"a": 1})").at(1), std::out_of_range);
}

// ITERATION

TEST_F(Internal_JsonTest, IteratesArrayElements)
{
    JsonNode n = parse("[10, 20, 30]");
    double sum = 0.0;
    for (const JsonNode& c : n) sum += c.asNumber();
    EXPECT_DOUBLE_EQ(sum, 60.0);
}

TEST_F(Internal_JsonTest, IteratesObjectMembers)
{
    JsonNode n = parse(R"({"a": 1, "b": 2})");
    std::string keys;
    for (const JsonNode& c : n) keys += c.name;
    EXPECT_EQ(keys, "ab");
}

TEST_F(Internal_JsonTest, IteratingEmptyContainerVisitsNothing)
{
    int count = 0;
    for (const JsonNode& c : parse("[]")) { (void)c; ++count; }
    for (const JsonNode& c : parse("{}")) { (void)c; ++count; }
    EXPECT_EQ(count, 0);
}

// WHITESPACE AND TRAILING DATA

TEST_F(Internal_JsonTest, SkipsWhitespaceAroundTokens)
{
    JsonNode n = parse("  {\n\t\"a\" :\t[ 1 ,\n2 ]\n}\n");
    ASSERT_EQ(n.size(), 1u);
    EXPECT_EQ(n["a"].size(), 2u);
}

TEST_F(Internal_JsonTest, RejectsTrailingData)
{
    EXPECT_THROW(parse("{} junk"), std::runtime_error);
    EXPECT_THROW(parse("1 2"), std::runtime_error);
}

TEST_F(Internal_JsonTest, RejectsEmptyInput)
{
    EXPECT_THROW(parse(""), std::runtime_error);
}

TEST_F(Internal_JsonTest, RejectsBadLiteral)
{
    EXPECT_THROW(parse("tru"), std::runtime_error);
    EXPECT_THROW(parse("nul"), std::runtime_error);
    EXPECT_THROW(parse("fals"), std::runtime_error);
}

// The parser reports the byte offset it stopped at, so errors point at input.
TEST_F(Internal_JsonTest, ErrorMessageNamesByteOffset)
{
    try
    {
        parse("[1, 2");
        FAIL() << "expected a parse error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("json parse error at byte"), std::string::npos);
    }
}

// OWNERSHIP

// Nodes copy the text they came from, so they stay valid once it is gone.
TEST_F(Internal_JsonTest, NodeOutlivesSourceText)
{
    JsonNode n;
    {
        std::string text = R"({"a": "value"})";
        n = parse(text);
    }
    EXPECT_EQ(n["a"].asString(), "value");
}

TEST_F(Internal_JsonTest, NodeIsMovable)
{
    JsonNode a = parse(R"({"a": [1, 2]})");
    JsonNode b = std::move(a);
    EXPECT_EQ(b["a"].size(), 2u);
}

TEST_F(Internal_JsonTest, NodeIsCopyable)
{
    JsonNode a = parse(R"({"a": 1})");
    JsonNode b = a;
    EXPECT_DOUBLE_EQ(b["a"].asNumber(), 1.0);
    EXPECT_DOUBLE_EQ(a["a"].asNumber(), 1.0);
}

// FILE LOADING

class Internal_JsonLoadTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        if (!path.empty()) std::remove(path.c_str());
    }

    // Writes text to a scratch file and returns its path.
    std::string writeTemp(const std::string& text)
    {
        path = std::string(::testing::TempDir()) + "Internal_JsonLoadTest.json";
        std::ofstream f(path, std::ios::binary);
        f << text;
        f.close();
        return path;
    }

    std::string path;
};

TEST_F(Internal_JsonLoadTest, LoadsDocumentFromFile)
{
    JsonNode n = utils::json::load(writeTemp(R"({"name": "router", "ports": [1, 2]})"));
    EXPECT_EQ(n["name"].asString(), "router");
    EXPECT_EQ(n["ports"].size(), 2u);
}

TEST_F(Internal_JsonLoadTest, ThrowsWhenFileMissing)
{
    EXPECT_THROW(utils::json::load("/nonexistent/path/to/file.json"), std::runtime_error);
}

TEST_F(Internal_JsonLoadTest, ThrowsOnMalformedFile)
{
    EXPECT_THROW(utils::json::load(writeTemp(R"({"a": )")), std::runtime_error);
}

TEST_F(Internal_JsonLoadTest, ThrowsOnEmptyFile)
{
    EXPECT_THROW(utils::json::load(writeTemp("")), std::runtime_error);
}

// PARSER REUSE

TEST_F(Internal_JsonTest, ParserInstanceParsesOneDocument)
{
    JsonParser p(R"({"a": 1})");
    JsonNode n = p.parseDocument();
    EXPECT_DOUBLE_EQ(n["a"].asNumber(), 1.0);
}

// REAL GRAMMAR SHAPE

// Mirrors the shape of Commands.json so the suite covers the real input.
TEST_F(Internal_JsonTest, ParsesCommandGrammarShape)
{
    JsonNode n = parse(R"({
        "(config)#": [
            {
                "name": "hostname",
                "description": "Set hostname",
                "properties": ["negate"],
                "subcommands": [
                    { "name": "WORD" }
                ]
            },
            { "name": "interface" }
        ]
    })");

    ASSERT_TRUE(n.isObject());
    const JsonNode& mode = n["(config)#"];
    ASSERT_TRUE(mode.isArray());
    ASSERT_EQ(mode.size(), 2u);

    const JsonNode& hostname = mode.at(0);
    EXPECT_EQ(hostname["name"].asString(), "hostname");
    EXPECT_EQ(hostname["description"].asString(), "Set hostname");
    ASSERT_EQ(hostname["properties"].size(), 1u);
    EXPECT_EQ(hostname["properties"].at(0).asString(), "negate");
    EXPECT_EQ(hostname["subcommands"].at(0)["name"].asString(), "WORD");

    EXPECT_FALSE(mode.at(1).contains("description"));
}
