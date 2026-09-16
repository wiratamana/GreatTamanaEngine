// Tier 1 tests for POST /import_asset's own pure request-parsing/response-
// building logic - task_manager/stl-parser-2 campaign, PHASE2
// (PHASE2_IMPORT_ASSET_NETWORK_ENDPOINT.md, Step 3.7). Mirrors
// tests/Network/NetworkRoutesTests.cpp's existing structure/naming
// conventions for ParseInstantiatePrimitiveRequest()/ParseSetEntityTrsRequest()
// exactly - no httplib/thread/AssetImportCommandBridge involved at all.

#include "Network/NetworkRoutes.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

using gte::Network::BuildGenericErrorResponseJson;
using gte::Network::BuildImportAssetResponseJson;
using gte::Network::ImportedAssetResponseView;
using gte::Network::ParseImportAssetRequest;
using gte::Network::ParsedImportAssetRequest;

TEST(ParseImportAssetRequestTests, FullyValidPayloadParsesBothFields)
{
    const ParsedImportAssetRequest result = ParseImportAssetRequest(
        R"({"source_path":"C:\\some\\file.stl","destination_folder":"Meshes/Terrain"})");

    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.sourcePath, "C:\\some\\file.stl");
    EXPECT_EQ(result.destinationFolder, "Meshes/Terrain");
}

TEST(ParseImportAssetRequestTests, DestinationFolderOmittedDefaultsToEmptyString)
{
    const ParsedImportAssetRequest result = ParseImportAssetRequest(R"({"source_path":"C:\\some\\file.stl"})");

    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.sourcePath, "C:\\some\\file.stl");
    EXPECT_EQ(result.destinationFolder, "");
}

TEST(ParseImportAssetRequestTests, DestinationFolderExplicitNullMeansOmitted)
{
    const ParsedImportAssetRequest result =
        ParseImportAssetRequest(R"({"source_path":"C:\\some\\file.stl","destination_folder":null})");

    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.destinationFolder, "");
}

TEST(ParseImportAssetRequestTests, DestinationFolderEmptyStringMeansOmitted)
{
    const ParsedImportAssetRequest result =
        ParseImportAssetRequest(R"({"source_path":"C:\\some\\file.stl","destination_folder":""})");

    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.destinationFolder, "");
}

TEST(ParseImportAssetRequestTests, MalformedJsonTextFails)
{
    const ParsedImportAssetRequest result = ParseImportAssetRequest("{not valid json");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "malformed JSON body");
}

TEST(ParseImportAssetRequestTests, TopLevelArrayFails)
{
    const ParsedImportAssetRequest result = ParseImportAssetRequest("[]");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "request body must be a JSON object");
}

TEST(ParseImportAssetRequestTests, MissingSourcePathFails)
{
    const ParsedImportAssetRequest result = ParseImportAssetRequest(R"({})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: source_path");
}

TEST(ParseImportAssetRequestTests, EmptyStringSourcePathFails)
{
    const ParsedImportAssetRequest result = ParseImportAssetRequest(R"({"source_path":""})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: source_path");
}

TEST(ParseImportAssetRequestTests, SourcePathNotAStringFails)
{
    const ParsedImportAssetRequest result = ParseImportAssetRequest(R"({"source_path":42})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: source_path");
}

TEST(ParseImportAssetRequestTests, DestinationFolderNonStringTypeFails)
{
    const ParsedImportAssetRequest result =
        ParseImportAssetRequest(R"({"source_path":"C:\\some\\file.stl","destination_folder":42})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "destination_folder must be a string");
}

TEST(ParseImportAssetRequestTests, ExtraUnrecognizedFieldIsIgnored)
{
    const ParsedImportAssetRequest result =
        ParseImportAssetRequest(R"({"source_path":"C:\\some\\file.stl","some_future_field":123})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.sourcePath, "C:\\some\\file.stl");
}

TEST(BuildImportAssetResponseJsonTests, FullRoundTripOfEveryField)
{
    ImportedAssetResponseView view;
    view.message = "imported OK";
    view.finalRelativePath = "Meshes/Terrain/terrain.gta";
    view.finalAbsolutePath = "C:/Project/Meshes/Terrain/terrain.gta";
    view.guid = "12345678-1234-1234-1234-123456789012";
    view.convertedToMeshAsset = true;
    view.meshSourceFormat = "stl";
    view.convertedToKtx2 = false;
    view.convertedToMotionAsset = false;
    view.meshVertexCount = 3136374;
    view.meshTriangleCount = 1045458;

    const std::string body = BuildImportAssetResponseJson(view);
    const nlohmann::json parsed = nlohmann::json::parse(body);

    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["message"], "imported OK");
    EXPECT_EQ(parsed["final_relative_path"], "Meshes/Terrain/terrain.gta");
    EXPECT_EQ(parsed["final_absolute_path"], "C:/Project/Meshes/Terrain/terrain.gta");
    EXPECT_EQ(parsed["guid"], "12345678-1234-1234-1234-123456789012");
    EXPECT_EQ(parsed["converted_to_mesh_asset"], true);
    EXPECT_EQ(parsed["mesh_source_format"], "stl");
    EXPECT_EQ(parsed["converted_to_ktx2"], false);
    EXPECT_EQ(parsed["converted_to_motion_asset"], false);
    EXPECT_EQ(parsed["mesh_vertex_count"], 3136374);
    EXPECT_EQ(parsed["mesh_triangle_count"], 1045458);
}

TEST(BuildImportAssetResponseJsonTests, NotConvertedToMeshAssetStillProducesEmptySourceFormat)
{
    ImportedAssetResponseView view;
    view.message = "copied as-is";
    view.finalRelativePath = "Docs/readme.txt";
    view.finalAbsolutePath = "C:/Project/Docs/readme.txt";
    view.guid = "";
    view.convertedToMeshAsset = false;
    view.meshSourceFormat = "";
    view.convertedToKtx2 = false;
    view.convertedToMotionAsset = false;
    view.meshVertexCount = 0;
    view.meshTriangleCount = 0;

    const std::string body = BuildImportAssetResponseJson(view);
    const nlohmann::json parsed = nlohmann::json::parse(body);

    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["converted_to_mesh_asset"], false);
    EXPECT_EQ(parsed["mesh_source_format"], "");
    EXPECT_EQ(parsed["guid"], "");
    EXPECT_EQ(parsed["mesh_vertex_count"], 0);
    EXPECT_EQ(parsed["mesh_triangle_count"], 0);
}

TEST(BuildImportAssetResponseJsonTests, MessageWithQuoteRoundTripsThroughJson)
{
    ImportedAssetResponseView view;
    view.message = R"(bad "quote" in message)";
    const std::string body = BuildImportAssetResponseJson(view);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["message"].get<std::string>(), R"(bad "quote" in message)");
}

TEST(BuildGenericErrorResponseJsonTests, UsedDirectlyForImportAssetFailurePath)
{
    // NetworkServer.cpp's own /import_asset route handler calls
    // BuildGenericErrorResponseJson() DIRECTLY for every failure case (bad
    // JSON, bridge unavailable, Project panel unavailable, timeout, and the
    // import itself failing) - never a dedicated
    // "BuildImportAssetErrorResponseJson()" - this test just re-confirms that
    // shared builder's shape from this endpoint's own perspective.
    const std::string body = BuildGenericErrorResponseJson("source file not found");
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], false);
    EXPECT_EQ(parsed["error"], "source file not found");
}

} // namespace
