// Tier 1 tests for POST /instantiate_asset's own pure request-parsing/
// response-building logic - task_manager/stl-parser-2 campaign, PHASE4
// (PHASE4_INSTANTIATE_ASSET_NETWORK_ENDPOINT.md, Step 3.4). Mirrors
// tests/Network/NetworkRoutesImportAssetTests.cpp's own structure/naming
// conventions exactly - no httplib/thread/EngineCommandBridge involved at
// all.

#include "Network/NetworkRoutes.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

namespace {

using gte::Network::BuildGenericErrorResponseJson;
using gte::Network::BuildInstantiateAssetResponseJson;
using gte::Network::ParseInstantiateAssetRequest;
using gte::Network::ParsedInstantiateAssetRequest;

TEST(ParseInstantiateAssetRequestTests, ValidPayloadParsesGtaPath)
{
    const ParsedInstantiateAssetRequest result =
        ParseInstantiateAssetRequest(R"({"gta_path":"C:\\Project\\Meshes\\terrain.gta"})");

    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.gtaPath, "C:\\Project\\Meshes\\terrain.gta");
}

TEST(ParseInstantiateAssetRequestTests, MalformedJsonTextFails)
{
    const ParsedInstantiateAssetRequest result = ParseInstantiateAssetRequest("{not valid json");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "malformed JSON body");
}

TEST(ParseInstantiateAssetRequestTests, TopLevelArrayFails)
{
    const ParsedInstantiateAssetRequest result = ParseInstantiateAssetRequest("[]");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "request body must be a JSON object");
}

TEST(ParseInstantiateAssetRequestTests, MissingGtaPathFails)
{
    const ParsedInstantiateAssetRequest result = ParseInstantiateAssetRequest(R"({})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: gta_path");
}

TEST(ParseInstantiateAssetRequestTests, EmptyStringGtaPathFails)
{
    const ParsedInstantiateAssetRequest result = ParseInstantiateAssetRequest(R"({"gta_path":""})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: gta_path");
}

TEST(ParseInstantiateAssetRequestTests, NonStringGtaPathFails)
{
    const ParsedInstantiateAssetRequest result = ParseInstantiateAssetRequest(R"({"gta_path":42})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: gta_path");
}

TEST(ParseInstantiateAssetRequestTests, ExtraUnrecognizedFieldIsIgnored)
{
    const ParsedInstantiateAssetRequest result =
        ParseInstantiateAssetRequest(R"({"gta_path":"C:\\terrain.gta","some_future_field":123})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.gtaPath, "C:\\terrain.gta");
}

ParsedInstantiateAssetRequest ParseWithName(const std::string& nameJson)
{
    return ParseInstantiateAssetRequest(R"({"gta_path":"C:\\a.gta","name":)" + nameJson + "}");
}

TEST(ParseInstantiateAssetRequestTests, ValidPayloadWithNameParsesRequestedName)
{
    const ParsedInstantiateAssetRequest result = ParseWithName(R"("Hero")");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.requestedName, "Hero");
}

TEST(ParseInstantiateAssetRequestTests, NameAbsentGivesEmpty)
{
    const ParsedInstantiateAssetRequest result = ParseInstantiateAssetRequest(R"({"gta_path":"C:\\a.gta"})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_TRUE(result.requestedName.empty());
}

TEST(ParseInstantiateAssetRequestTests, NameNullGivesEmpty)
{
    const ParsedInstantiateAssetRequest result = ParseWithName("null");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_TRUE(result.requestedName.empty());
}

TEST(ParseInstantiateAssetRequestTests, NameEmptyStringGivesEmptyAndValid)
{
    const ParsedInstantiateAssetRequest result = ParseWithName(R"("")");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_TRUE(result.requestedName.empty());
}

TEST(ParseInstantiateAssetRequestTests, NameWhitespaceOnlyGivesEmptyAndValid)
{
    const ParsedInstantiateAssetRequest result = ParseWithName(R"("  \t\n ")");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_TRUE(result.requestedName.empty());
}

TEST(ParseInstantiateAssetRequestTests, NameIsTrimmed)
{
    const ParsedInstantiateAssetRequest result = ParseWithName(R"("  Hero  ")");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.requestedName, "Hero");
}

TEST(ParseInstantiateAssetRequestTests, NameNumberFails)
{
    const ParsedInstantiateAssetRequest result = ParseWithName("42");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "name must be a string");
}

TEST(ParseInstantiateAssetRequestTests, NameWithNewlineInsideFails)
{
    const ParsedInstantiateAssetRequest result = ParseWithName(R"("a\nb")");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "name must not contain control characters");
}

TEST(ParseInstantiateAssetRequestTests, NameExactly128BytesIsValid)
{
    const ParsedInstantiateAssetRequest result = ParseWithName("\"" + std::string(128, 'a') + "\"");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.requestedName.size(), 128u);
}

TEST(ParseInstantiateAssetRequestTests, Name129BytesFails)
{
    const ParsedInstantiateAssetRequest result = ParseWithName("\"" + std::string(129, 'a') + "\"");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "name must be at most 128 bytes");
}

TEST(ParseInstantiateAssetRequestTests, NameWithSpacesAndParenthesesIsKept)
{
    const ParsedInstantiateAssetRequest result = ParseWithName(R"j("Furina (1)")j");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.requestedName, "Furina (1)");
}

TEST(ParseInstantiateAssetRequestTests, GtaPathErrorIsReportedBeforeNameError)
{
    const ParsedInstantiateAssetRequest result = ParseInstantiateAssetRequest(R"({"gta_path":"","name":42})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: gta_path");
}

TEST(BuildInstantiateAssetResponseJsonTests, NameAdjustedFlagIsEmitted)
{
    const nlohmann::json adjusted = nlohmann::json::parse(BuildInstantiateAssetResponseJson(1, 0, "Furina (1)", true));
    EXPECT_EQ(adjusted["name_adjusted"], true);

    const nlohmann::json plain = nlohmann::json::parse(BuildInstantiateAssetResponseJson(1, 0, "Furina", false));
    EXPECT_EQ(plain["name_adjusted"], false);

    const nlohmann::json defaulted = nlohmann::json::parse(BuildInstantiateAssetResponseJson(1, 0, "Furina"));
    EXPECT_EQ(defaulted["name_adjusted"], false);
}

TEST(BuildInstantiateAssetResponseJsonTests, ExactJsonShape)
{
    const std::string body = BuildInstantiateAssetResponseJson(7, 2, "terrain");
    const nlohmann::json parsed = nlohmann::json::parse(body);

    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["entity"]["index"], 7);
    EXPECT_EQ(parsed["entity"]["generation"], 2);
    EXPECT_EQ(parsed["name"], "terrain");
}

TEST(BuildInstantiateAssetResponseJsonTests, NameWithQuoteRoundTripsThroughJson)
{
    const std::string body = BuildInstantiateAssetResponseJson(0, 0, R"(bad "quote" name)");
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["name"].get<std::string>(), R"(bad "quote" name)");
}

TEST(BuildGenericErrorResponseJsonTests, UsedDirectlyForInstantiateAssetFailurePath)
{
    // NetworkServer.cpp's own /instantiate_asset route handler calls
    // BuildGenericErrorResponseJson() DIRECTLY for every failure case (bad
    // JSON, bridge unavailable, already-pending, timeout, and the spawn
    // itself failing) - never a dedicated
    // "BuildInstantiateAssetErrorResponseJson()" - this test just re-confirms
    // that shared builder's shape from this endpoint's own perspective.
    const std::string body = BuildGenericErrorResponseJson("mesh asset not found");
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], false);
    EXPECT_EQ(parsed["error"], "mesh asset not found");
}

} // namespace
