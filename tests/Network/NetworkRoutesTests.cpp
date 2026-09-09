#include "Network/NetworkRoutes.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

using gte::Network::CaptureResponseFormat;
using gte::Network::ResolveCaptureResponseFormat;

TEST(NetworkRoutesTests, HandleHelloWorldReturnsExactContractedString)
{
    EXPECT_EQ(gte::Network::HandleHelloWorld(), "hello world");
}

// network-impl-2 campaign, Phase 3
// (PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md) - table-driven
// coverage of every precedence rule ResolveCaptureResponseFormat() documents
// in NetworkRoutes.h.

struct ResolveCaptureResponseFormatCase {
    std::string queryFormat;
    std::string acceptHeader;
    CaptureResponseFormat expected;
};

class ResolveCaptureResponseFormatTest : public ::testing::TestWithParam<ResolveCaptureResponseFormatCase> {};

TEST_P(ResolveCaptureResponseFormatTest, MatchesLockedPrecedenceRules)
{
    const ResolveCaptureResponseFormatCase& testCase = GetParam();
    EXPECT_EQ(ResolveCaptureResponseFormat(testCase.queryFormat, testCase.acceptHeader), testCase.expected);
}

INSTANTIATE_TEST_SUITE_P(NetworkRoutesTests, ResolveCaptureResponseFormatTest,
    ::testing::Values(
        // ?format=png always wins, regardless of Accept.
        ResolveCaptureResponseFormatCase{ "png", "", CaptureResponseFormat::RawPng },
        ResolveCaptureResponseFormatCase{ "png", "application/json", CaptureResponseFormat::RawPng },
        // ?format=base64 / ?format=json always win, regardless of Accept.
        ResolveCaptureResponseFormatCase{ "base64", "", CaptureResponseFormat::JsonBase64 },
        ResolveCaptureResponseFormatCase{ "json", "text/plain", CaptureResponseFormat::JsonBase64 },
        // An unrecognized ?format= value is not an error - falls back to RawPng.
        ResolveCaptureResponseFormatCase{ "bogus", "application/json", CaptureResponseFormat::RawPng },
        // No ?format= at all - Accept decides.
        ResolveCaptureResponseFormatCase{ "", "application/json", CaptureResponseFormat::JsonBase64 },
        ResolveCaptureResponseFormatCase{ "", "text/html,application/json;q=0.9", CaptureResponseFormat::JsonBase64 },
        ResolveCaptureResponseFormatCase{ "", "text/plain", CaptureResponseFormat::RawPng },
        ResolveCaptureResponseFormatCase{ "", "", CaptureResponseFormat::RawPng }));

TEST(NetworkRoutesTests, BuildCaptureJsonBodyProducesExactExpectedShape)
{
    const std::string body = gte::Network::BuildCaptureJsonBody(64, 32, "QUJD");
    EXPECT_EQ(body, "{\"width\":64,\"height\":32,\"format\":\"png\",\"data_base64\":\"QUJD\"}");
}

TEST(NetworkRoutesTests, BuildCaptureJsonBodyHandlesEmptyBase64)
{
    const std::string body = gte::Network::BuildCaptureJsonBody(0, 0, "");
    EXPECT_EQ(body, "{\"width\":0,\"height\":0,\"format\":\"png\",\"data_base64\":\"\"}");
}

// --- network-impl-3 campaign
// (PHASE1_JSON_DEPENDENCY_AND_REQUEST_PARSING.md) - JSON request parsing +
// response building.

using gte::Network::BuildDeleteEntityResponseJson;
using gte::Network::BuildGenericErrorResponseJson;
using gte::Network::BuildInstantiatePrimitiveResponseJson;
using gte::Network::ParseDeleteEntityRequest;
using gte::Network::ParseInstantiatePrimitiveRequest;
using gte::Network::ParsedDeleteEntityRequest;
using gte::Network::ParsedInstantiatePrimitiveRequest;

TEST(ParseInstantiatePrimitiveRequestTests, FullyValidPayloadParsesEveryField)
{
    const ParsedInstantiatePrimitiveRequest result = ParseInstantiatePrimitiveRequest(
        R"({"shape":"cube","name":"MyCube","world_position":{"x":1.5,"y":2.5,"z":-3.0},"parent":"Root"})");

    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.shape, "cube");
    EXPECT_EQ(result.name, "MyCube");
    EXPECT_FLOAT_EQ(result.worldX, 1.5f);
    EXPECT_FLOAT_EQ(result.worldY, 2.5f);
    EXPECT_FLOAT_EQ(result.worldZ, -3.0f);
    EXPECT_TRUE(result.hasParent);
    EXPECT_EQ(result.parentName, "Root");
}

TEST(ParseInstantiatePrimitiveRequestTests, WorldPositionAbsentDefaultsToZero)
{
    const ParsedInstantiatePrimitiveRequest result =
        ParseInstantiatePrimitiveRequest(R"({"shape":"cube","name":"MyCube"})");

    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FLOAT_EQ(result.worldX, 0.0f);
    EXPECT_FLOAT_EQ(result.worldY, 0.0f);
    EXPECT_FLOAT_EQ(result.worldZ, 0.0f);
    EXPECT_FALSE(result.hasParent);
}

TEST(ParseInstantiatePrimitiveRequestTests, WorldPositionPartialAxesDefaultMissingOnesToZero)
{
    const ParsedInstantiatePrimitiveRequest result = ParseInstantiatePrimitiveRequest(
        R"({"shape":"cube","name":"MyCube","world_position":{"y":9.0}})");

    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FLOAT_EQ(result.worldX, 0.0f);
    EXPECT_FLOAT_EQ(result.worldY, 9.0f);
    EXPECT_FLOAT_EQ(result.worldZ, 0.0f);
}

TEST(ParseInstantiatePrimitiveRequestTests, ParentAbsentMeansNoParent)
{
    const ParsedInstantiatePrimitiveRequest result =
        ParseInstantiatePrimitiveRequest(R"({"shape":"cube","name":"MyCube"})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FALSE(result.hasParent);
}

TEST(ParseInstantiatePrimitiveRequestTests, ParentExplicitNullMeansNoParent)
{
    const ParsedInstantiatePrimitiveRequest result =
        ParseInstantiatePrimitiveRequest(R"({"shape":"cube","name":"MyCube","parent":null})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FALSE(result.hasParent);
}

TEST(ParseInstantiatePrimitiveRequestTests, ParentEmptyStringMeansNoParent)
{
    const ParsedInstantiatePrimitiveRequest result =
        ParseInstantiatePrimitiveRequest(R"({"shape":"cube","name":"MyCube","parent":""})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FALSE(result.hasParent);
}

TEST(ParseInstantiatePrimitiveRequestTests, ParentNonEmptyStringMeansHasParent)
{
    const ParsedInstantiatePrimitiveRequest result =
        ParseInstantiatePrimitiveRequest(R"({"shape":"cube","name":"MyCube","parent":"Root"})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_TRUE(result.hasParent);
    EXPECT_EQ(result.parentName, "Root");
}

TEST(ParseInstantiatePrimitiveRequestTests, MissingShapeFails)
{
    const ParsedInstantiatePrimitiveRequest result = ParseInstantiatePrimitiveRequest(R"({"name":"MyCube"})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: shape");
}

TEST(ParseInstantiatePrimitiveRequestTests, ShapeNotAStringFails)
{
    const ParsedInstantiatePrimitiveRequest result =
        ParseInstantiatePrimitiveRequest(R"({"shape":42,"name":"MyCube"})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: shape");
}

TEST(ParseInstantiatePrimitiveRequestTests, EmptyStringShapeFails)
{
    const ParsedInstantiatePrimitiveRequest result =
        ParseInstantiatePrimitiveRequest(R"({"shape":"","name":"MyCube"})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: shape");
}

TEST(ParseInstantiatePrimitiveRequestTests, MissingNameFails)
{
    const ParsedInstantiatePrimitiveRequest result = ParseInstantiatePrimitiveRequest(R"({"shape":"cube"})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: name");
}

TEST(ParseInstantiatePrimitiveRequestTests, EmptyStringNameFails)
{
    const ParsedInstantiatePrimitiveRequest result =
        ParseInstantiatePrimitiveRequest(R"({"shape":"cube","name":""})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: name");
}

TEST(ParseInstantiatePrimitiveRequestTests, WorldPositionNotAnObjectFails)
{
    const ParsedInstantiatePrimitiveRequest result = ParseInstantiatePrimitiveRequest(
        R"({"shape":"cube","name":"MyCube","world_position":"nope"})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "world_position must be an object");
}

TEST(ParseInstantiatePrimitiveRequestTests, WorldPositionXNotANumberFails)
{
    const ParsedInstantiatePrimitiveRequest result = ParseInstantiatePrimitiveRequest(
        R"({"shape":"cube","name":"MyCube","world_position":{"x":"nope"}})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "world_position.x must be a number");
}

TEST(ParseInstantiatePrimitiveRequestTests, ParentWrongTypeFails)
{
    const ParsedInstantiatePrimitiveRequest result =
        ParseInstantiatePrimitiveRequest(R"({"shape":"cube","name":"MyCube","parent":42})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "parent must be a string or null");
}

TEST(ParseInstantiatePrimitiveRequestTests, MalformedJsonTextFails)
{
    const ParsedInstantiatePrimitiveRequest result = ParseInstantiatePrimitiveRequest("{not valid json");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "malformed JSON body");
}

TEST(ParseInstantiatePrimitiveRequestTests, TopLevelArrayFails)
{
    const ParsedInstantiatePrimitiveRequest result = ParseInstantiatePrimitiveRequest("[]");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "request body must be a JSON object");
}

TEST(ParseInstantiatePrimitiveRequestTests, TopLevelNumberFails)
{
    const ParsedInstantiatePrimitiveRequest result = ParseInstantiatePrimitiveRequest("42");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "request body must be a JSON object");
}

TEST(ParseInstantiatePrimitiveRequestTests, ExtraUnrecognizedFieldIsIgnored)
{
    const ParsedInstantiatePrimitiveRequest result = ParseInstantiatePrimitiveRequest(
        R"({"shape":"cube","name":"MyCube","some_future_field":123})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.shape, "cube");
    EXPECT_EQ(result.name, "MyCube");
}

TEST(ParseDeleteEntityRequestTests, ValidPayloadParses)
{
    const ParsedDeleteEntityRequest result = ParseDeleteEntityRequest(R"({"name":"MyCube"})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.name, "MyCube");
}

TEST(ParseDeleteEntityRequestTests, MissingNameFails)
{
    const ParsedDeleteEntityRequest result = ParseDeleteEntityRequest(R"({})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: name");
}

TEST(ParseDeleteEntityRequestTests, EmptyStringNameFails)
{
    const ParsedDeleteEntityRequest result = ParseDeleteEntityRequest(R"({"name":""})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: name");
}

TEST(ParseDeleteEntityRequestTests, MalformedJsonFails)
{
    const ParsedDeleteEntityRequest result = ParseDeleteEntityRequest("{not valid json");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "malformed JSON body");
}

TEST(ParseDeleteEntityRequestTests, NonObjectTopLevelFails)
{
    const ParsedDeleteEntityRequest result = ParseDeleteEntityRequest("\"just a string\"");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "request body must be a JSON object");
}

TEST(BuildResponseJsonTests, InstantiatePrimitiveSuccessShape)
{
    const std::string body = BuildInstantiatePrimitiveResponseJson(
        /*success=*/true, /*errorMessage=*/"", /*entityIndex=*/7, /*entityGeneration=*/2,
        /*resolvedName=*/"MyCube", /*parentRequestedButNotFound=*/false, /*requestedParentName=*/"");

    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["entity"]["index"], 7);
    EXPECT_EQ(parsed["entity"]["generation"], 2);
    EXPECT_EQ(parsed["name"], "MyCube");
    EXPECT_EQ(parsed["parent_requested_but_not_found"], false);
    EXPECT_EQ(parsed["requested_parent_name"], "");
}

TEST(BuildResponseJsonTests, InstantiatePrimitiveFailureShapeMatchesGenericError)
{
    const std::string body =
        BuildInstantiatePrimitiveResponseJson(false, "boom", 0, 0, "", false, "");
    EXPECT_EQ(body, BuildGenericErrorResponseJson("boom"));
}

TEST(BuildResponseJsonTests, InstantiatePrimitiveNameWithQuoteRoundTripsThroughJson)
{
    const std::string nameWithQuote = R"(My"Cube)";
    const std::string body =
        BuildInstantiatePrimitiveResponseJson(true, "", 1, 0, nameWithQuote, false, "");

    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["name"].get<std::string>(), nameWithQuote);
}

TEST(BuildResponseJsonTests, DeleteEntitySuccessShape)
{
    const std::string body = BuildDeleteEntityResponseJson(true, "", 3, 1);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["entity"]["index"], 3);
    EXPECT_EQ(parsed["entity"]["generation"], 1);
}

TEST(BuildResponseJsonTests, DeleteEntityFailureShapeMatchesGenericError)
{
    const std::string body = BuildDeleteEntityResponseJson(false, "not found", 0, 0);
    EXPECT_EQ(body, BuildGenericErrorResponseJson("not found"));
}

TEST(BuildResponseJsonTests, GenericErrorResponseShape)
{
    const std::string body = BuildGenericErrorResponseJson("something went wrong");
    EXPECT_EQ(body, R"({"error":"something went wrong","success":false})");
}

TEST(BuildResponseJsonTests, GenericErrorResponseEscapesQuotesAndBackslashes)
{
    const std::string messageWithSpecialChars = R"(bad "quote" and \backslash\)";
    const std::string body = BuildGenericErrorResponseJson(messageWithSpecialChars);

    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], false);
    EXPECT_EQ(parsed["error"].get<std::string>(), messageWithSpecialChars);
}

} // namespace
