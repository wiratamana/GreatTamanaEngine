// Tier 1 tests for POST /instantiate_asset's own pure request-parsing/
// response-building logic - task_manager/stl-parser-2 campaign, PHASE4
// (PHASE4_INSTANTIATE_ASSET_NETWORK_ENDPOINT.md, Step 3.4). Mirrors
// tests/Network/NetworkRoutesImportAssetTests.cpp's own structure/naming
// conventions exactly - no httplib/thread/EngineCommandBridge involved at
// all.

#include "Network/NetworkRoutes.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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
