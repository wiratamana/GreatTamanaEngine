#include "Network/NetworkRoutes.h"

// PHASE3 (editor-core-separation-1 campaign,
// PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md) - NetworkRoutes.h no longer
// includes Editor/Logger.h (see that header's own updated comment), but
// several tests below (GetLogsEndToEndTests/ClearLogsEndToEndTests/
// ParseGetLogsQueryTests) still need the real Editor::Logger class
// directly. Matches Network/NetworkRoutes.cpp's own identical fix.
#include "Editor/Logger.h"

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

// --- network-impl-4 campaign, Phase 5
// (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md) -
// GET /get_texture + GET /list_textures.

using gte::Network::BuildListTexturesResponseJson;
using gte::Network::BuildTextureCaptureJsonBody;
using gte::Network::ParseGetTextureQuery;
using gte::Network::ParsedGetTextureQuery;
using gte::Network::TextureListEntryView;

TEST(ParseGetTextureQueryTests, MissingTextureNameFails)
{
    const ParsedGetTextureQuery result = ParseGetTextureQuery("", "");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or empty required query parameter: texture_name");
}

TEST(ParseGetTextureQueryTests, EmptyTextureNameFails)
{
    const ParsedGetTextureQuery result = ParseGetTextureQuery("", "color");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or empty required query parameter: texture_name");
}

TEST(ParseGetTextureQueryTests, ChannelAbsentDefaultsToColor)
{
    const ParsedGetTextureQuery result = ParseGetTextureQuery("GameView", "");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.textureName, "GameView");
    EXPECT_FALSE(result.wantsDepth);
}

TEST(ParseGetTextureQueryTests, ChannelColorMeansWantsDepthFalse)
{
    const ParsedGetTextureQuery result = ParseGetTextureQuery("GameView", "color");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FALSE(result.wantsDepth);
}

TEST(ParseGetTextureQueryTests, ChannelDepthMeansWantsDepthTrue)
{
    const ParsedGetTextureQuery result = ParseGetTextureQuery("GameView", "depth");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_TRUE(result.wantsDepth);
}

struct ParseGetTextureQueryInvalidChannelCase {
    std::string channel;
};

class ParseGetTextureQueryInvalidChannelTest
    : public ::testing::TestWithParam<ParseGetTextureQueryInvalidChannelCase> {};

TEST_P(ParseGetTextureQueryInvalidChannelTest, RejectsNonLowercaseOrUnknownChannelValues)
{
    const ParsedGetTextureQuery result = ParseGetTextureQuery("GameView", GetParam().channel);
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "invalid channel - must be \"color\" or \"depth\"");
}

INSTANTIATE_TEST_SUITE_P(NetworkRoutesTests, ParseGetTextureQueryInvalidChannelTest,
    ::testing::Values(
        ParseGetTextureQueryInvalidChannelCase{ "Depth" },
        ParseGetTextureQueryInvalidChannelCase{ "COLOR" },
        ParseGetTextureQueryInvalidChannelCase{ "bogus" }));

TEST(BuildTextureCaptureJsonBodyTests, PopulatedCallProducesExpectedFields)
{
    const std::string body = BuildTextureCaptureJsonBody(64, 32, "QUJD", 7);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["width"], 64);
    EXPECT_EQ(parsed["height"], 32);
    EXPECT_EQ(parsed["format"], "png");
    EXPECT_EQ(parsed["data_base64"], "QUJD");
    EXPECT_EQ(parsed["frames_since_update"], 7);
}

TEST(BuildListTexturesResponseJsonTests, EmptyListProducesLiteralEmptyShape)
{
    const std::string body = BuildListTexturesResponseJson({});
    EXPECT_EQ(body, R"({"textures":[]})");
}

TEST(BuildListTexturesResponseJsonTests, MultipleEntriesRoundTripEveryField)
{
    const std::vector<TextureListEntryView> entries = {
        TextureListEntryView{ "GameView", "synchronous", "B8G8R8A8_UNORM", 1280, 720, true, 0 },
        TextureListEntryView{ "Swapchain", "pipelined", "B8G8R8A8_SRGB", 1920, 1080, false, 3 },
    };
    const std::string body = BuildListTexturesResponseJson(entries);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    ASSERT_EQ(parsed["textures"].size(), 2u);

    EXPECT_EQ(parsed["textures"][0]["name"], "GameView");
    EXPECT_EQ(parsed["textures"][0]["regime"], "synchronous");
    EXPECT_EQ(parsed["textures"][0]["format"], "B8G8R8A8_UNORM");
    EXPECT_EQ(parsed["textures"][0]["width"], 1280);
    EXPECT_EQ(parsed["textures"][0]["height"], 720);
    EXPECT_EQ(parsed["textures"][0]["has_depth"], true);
    EXPECT_EQ(parsed["textures"][0]["frames_since_update"], 0);
    // network-impl-6 campaign, Phase 5 - a default-constructed
    // TextureListEntryView (no kind/depth supplied at the call site above)
    // must still emit its "texture2d"/0 defaults explicitly.
    EXPECT_EQ(parsed["textures"][0]["kind"], "texture2d");
    EXPECT_EQ(parsed["textures"][0]["depth"], 0);

    EXPECT_EQ(parsed["textures"][1]["name"], "Swapchain");
    EXPECT_EQ(parsed["textures"][1]["regime"], "pipelined");
    EXPECT_EQ(parsed["textures"][1]["format"], "B8G8R8A8_SRGB");
    EXPECT_EQ(parsed["textures"][1]["width"], 1920);
    EXPECT_EQ(parsed["textures"][1]["height"], 1080);
    EXPECT_EQ(parsed["textures"][1]["has_depth"], false);
    EXPECT_EQ(parsed["textures"][1]["frames_since_update"], 3);
    EXPECT_EQ(parsed["textures"][1]["kind"], "texture2d");
    EXPECT_EQ(parsed["textures"][1]["depth"], 0);
}

TEST(BuildListTexturesResponseJsonTests, VolumeEntryReportsTexture3dKindAndDepth)
{
    // network-impl-6 campaign, Phase 5
    // (task_manager/network-impl-6/PHASE5_LIST_TEXTURES_VOLUME_SURFACING.md) -
    // a volume texture's entry always has has_depth == false (no
    // depth-companion concept at all - see VolumeTarget.h) but a real,
    // non-zero "depth" field (its own Z/texel-count dimension).
    TextureListEntryView entry{ "AtmosphereAerialPerspectiveVolume_GameView", "synchronous",
        "R16G16B16A16_SFLOAT", 128, 128, false, 0 };
    entry.kind = "texture3d";
    entry.depth = 32;
    const std::vector<TextureListEntryView> entries = { entry };

    const std::string body = BuildListTexturesResponseJson(entries);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    ASSERT_EQ(parsed["textures"].size(), 1u);
    EXPECT_EQ(parsed["textures"][0]["name"], "AtmosphereAerialPerspectiveVolume_GameView");
    EXPECT_EQ(parsed["textures"][0]["regime"], "synchronous");
    EXPECT_EQ(parsed["textures"][0]["format"], "R16G16B16A16_SFLOAT");
    EXPECT_EQ(parsed["textures"][0]["width"], 128);
    EXPECT_EQ(parsed["textures"][0]["height"], 128);
    EXPECT_EQ(parsed["textures"][0]["has_depth"], false);
    EXPECT_EQ(parsed["textures"][0]["kind"], "texture3d");
    EXPECT_EQ(parsed["textures"][0]["depth"], 32);
}

TEST(BuildListTexturesResponseJsonTests, NameWithQuoteRoundTripsThroughJson)
{
    const std::vector<TextureListEntryView> entries = {
        TextureListEntryView{ R"(My"Texture)", "synchronous", "B8G8R8A8_UNORM", 4, 4, false, 0 },
    };
    const std::string body = BuildListTexturesResponseJson(entries);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["textures"][0]["name"].get<std::string>(), R"(My"Texture)");
}

// --- editor-core-separation-7 campaign, PHASE4
// (PHASE4_CROSS_THREAD_BRIDGE_AND_GET_RENDER_GRAPH_ENDPOINT.md) -
// GET /render_graph's own response builder. Hand-fabricates a small
// gte::rg::RenderGraphMetadata directly (mirrors
// tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp's own
// hand-fabrication style - no live RenderGraph/BuildRenderGraphMetadata()
// call needed here, since this test only exercises the to_json() ->
// dump() -> parse() round trip this route's own response body is built
// from).

using gte::Network::BuildRenderGraphMetadataResponseJson;

TEST(BuildRenderGraphMetadataResponseJsonTests, DefaultConstructedMetadataProducesExpectedEmptyShape)
{
    const gte::rg::RenderGraphMetadata metadata; // schemaVersion == 1, everything else empty/default.
    const std::string body = BuildRenderGraphMetadataResponseJson(metadata);
    const nlohmann::json parsed = nlohmann::json::parse(body);

    EXPECT_EQ(parsed["schema_version"].get<std::uint32_t>(), 1u);
    ASSERT_TRUE(parsed.contains("offscreen_regime"));
    // Note: regimeName is only populated by BuildRenderGraphMetadata() itself
    // (Renderer/RenderGraph/RenderGraphMetadataTests.cpp already covers
    // that) - a bare, default-constructed RenderGraphMetadata (as fabricated
    // here, with zero involvement from that function) correctly serializes
    // its still-default-constructed "" regimeName field, never a fabricated
    // fallback string.
    EXPECT_EQ(parsed["offscreen_regime"]["regime_name"].get<std::string>(), "");
    EXPECT_TRUE(parsed["offscreen_regime"]["passes"].empty());
    EXPECT_TRUE(parsed["offscreen_regime"]["resources"].empty());
    ASSERT_TRUE(parsed.contains("present_regime"));
    EXPECT_EQ(parsed["present_regime"]["regime_name"].get<std::string>(), "");
    EXPECT_TRUE(parsed["gpu_driven_batches"].empty());
    EXPECT_TRUE(parsed["render_features"].empty());
}

TEST(BuildRenderGraphMetadataResponseJsonTests, PopulatedMetadataRoundTripsEveryTopLevelField)
{
    gte::rg::RenderGraphMetadata metadata;
    metadata.schemaVersion = 1;

    gte::rg::RenderGraphPassMetadata pass;
    pass.name = "RenderOpaque";
    pass.isCulled = false;
    pass.kind = "Graphics";
    pass.category = "General";
    pass.drawKind = "DrawMesh";
    pass.viewScope = "Shared";
    pass.renderPassEvent = "Opaques";
    pass.reads.push_back(gte::rg::RenderGraphResourceRefMetadata{ "Depth", "Texture" });
    pass.writes.push_back(gte::rg::RenderGraphResourceRefMetadata{ "Color", "Texture" });
    pass.drawCallCount = 2;
    pass.triangleCount = 20;
    pass.gpuTimingText = "N/A";
    metadata.offscreenRegime.regimeName = "SynchronousImmediateReadback";
    metadata.offscreenRegime.passes.push_back(pass);

    metadata.presentRegime.regimeName = "PipelinedDeferredReadback";

    gte::GpuDrivenBatchDebugInfo batch;
    batch.batchName = "Batch0";
    batch.instanceCount = 4;
    metadata.gpuDrivenBatches.push_back(batch);

    gte::RenderFeatureDebugEntry feature;
    feature.name = "Vignette";
    feature.stage = "PreUI";
    feature.priority = 1;
    feature.blendMode = "Replace";
    metadata.renderFeatures.push_back(feature);

    const std::string body = BuildRenderGraphMetadataResponseJson(metadata);
    const nlohmann::json parsed = nlohmann::json::parse(body);

    ASSERT_EQ(parsed["offscreen_regime"]["passes"].size(), 1u);
    const nlohmann::json& jsonPass = parsed["offscreen_regime"]["passes"][0];
    EXPECT_EQ(jsonPass["name"].get<std::string>(), "RenderOpaque");
    EXPECT_FALSE(jsonPass["is_culled"].get<bool>());
    EXPECT_TRUE(jsonPass["tag_group_label"].is_null());
    EXPECT_EQ(jsonPass["draw_call_count"].get<std::uint32_t>(), 2u);
    EXPECT_EQ(jsonPass["triangle_count"].get<std::uint32_t>(), 20u);
    ASSERT_EQ(jsonPass["reads"].size(), 1u);
    EXPECT_EQ(jsonPass["reads"][0]["name"].get<std::string>(), "Depth");
    EXPECT_EQ(jsonPass["reads"][0]["kind"].get<std::string>(), "Texture");

    ASSERT_EQ(parsed["gpu_driven_batches"].size(), 1u);
    EXPECT_EQ(parsed["gpu_driven_batches"][0]["batch_name"].get<std::string>(), "Batch0");
    EXPECT_EQ(parsed["gpu_driven_batches"][0]["instance_count"].get<std::uint32_t>(), 4u);
    EXPECT_TRUE(parsed["gpu_driven_batches"][0]["visible_count"].is_null());

    ASSERT_EQ(parsed["render_features"].size(), 1u);
    EXPECT_EQ(parsed["render_features"][0]["name"].get<std::string>(), "Vignette");
    EXPECT_EQ(parsed["render_features"][0]["stage"].get<std::string>(), "PreUI");
    EXPECT_EQ(parsed["render_features"][0]["priority"].get<std::int32_t>(), 1);
    EXPECT_EQ(parsed["render_features"][0]["blend_mode"].get<std::string>(), "Replace");
}

// --- network-impl-5 campaign
// (PHASE1_NETWORK_ROUTES_REQUEST_PARSING_AND_RESPONSE_BUILDING.md) -
// POST /set_entity_trs + POST /instantiate_light request parsing/response
// building.

using gte::Network::BuildSetEntityTrsResponseJson;
using gte::Network::ParseInstantiateLightRequest;
using gte::Network::ParseSetEntityTrsRequest;
using gte::Network::ParsedInstantiateLightRequest;
using gte::Network::ParsedSetEntityTrsRequest;
using gte::Network::TransformSnapshotView;

TEST(ParseSetEntityTrsRequestTests, FullyValidPayloadParsesAllThreeGroups)
{
    const ParsedSetEntityTrsRequest result = ParseSetEntityTrsRequest(
        R"({"name":"MyLight","translation":{"x":1.0,"y":2.0,"z":3.0},)"
        R"("rotation_euler_degrees":{"x":30.0,"y":90.0,"z":0.0},)"
        R"("scale":{"x":2.0,"y":2.0,"z":2.0}})");

    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.name, "MyLight");
    ASSERT_TRUE(result.hasTranslation);
    EXPECT_FLOAT_EQ(result.translationX, 1.0f);
    EXPECT_FLOAT_EQ(result.translationY, 2.0f);
    EXPECT_FLOAT_EQ(result.translationZ, 3.0f);
    ASSERT_TRUE(result.hasRotationEulerDegrees);
    EXPECT_FLOAT_EQ(result.rotationPitchXDegrees, 30.0f);
    EXPECT_FLOAT_EQ(result.rotationYawYDegrees, 90.0f);
    EXPECT_FLOAT_EQ(result.rotationRollZDegrees, 0.0f);
    ASSERT_TRUE(result.hasScale);
    EXPECT_FLOAT_EQ(result.scaleX, 2.0f);
    EXPECT_FLOAT_EQ(result.scaleY, 2.0f);
    EXPECT_FLOAT_EQ(result.scaleZ, 2.0f);
}

TEST(ParseSetEntityTrsRequestTests, NoneOfTheThreeGroupsPresentIsStillValid)
{
    const ParsedSetEntityTrsRequest result = ParseSetEntityTrsRequest(R"({"name":"MyLight"})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FALSE(result.hasTranslation);
    EXPECT_FALSE(result.hasRotationEulerDegrees);
    EXPECT_FALSE(result.hasScale);
}

TEST(ParseSetEntityTrsRequestTests, AllThreeGroupsExplicitlyNullIsStillValid)
{
    const ParsedSetEntityTrsRequest result = ParseSetEntityTrsRequest(
        R"({"name":"MyLight","translation":null,"rotation_euler_degrees":null,"scale":null})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FALSE(result.hasTranslation);
    EXPECT_FALSE(result.hasRotationEulerDegrees);
    EXPECT_FALSE(result.hasScale);
}

TEST(ParseSetEntityTrsRequestTests, TranslationMissingOneAxisFails)
{
    const ParsedSetEntityTrsRequest result =
        ParseSetEntityTrsRequest(R"({"name":"MyLight","translation":{"x":1.0,"y":2.0}})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "translation must be an object with numeric x, y, and z fields");
}

TEST(ParseSetEntityTrsRequestTests, TranslationNotAnObjectFailsWithSameMessageAsMissingAxis)
{
    const ParsedSetEntityTrsRequest result =
        ParseSetEntityTrsRequest(R"({"name":"MyLight","translation":"nope"})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "translation must be an object with numeric x, y, and z fields");
}

TEST(ParseSetEntityTrsRequestTests, RotationEulerDegreesMissingOneAxisFails)
{
    const ParsedSetEntityTrsRequest result = ParseSetEntityTrsRequest(
        R"({"name":"MyLight","rotation_euler_degrees":{"x":1.0,"z":2.0}})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "rotation_euler_degrees must be an object with numeric x, y, and z fields");
}

TEST(ParseSetEntityTrsRequestTests, ScaleMissingOneAxisFails)
{
    const ParsedSetEntityTrsRequest result =
        ParseSetEntityTrsRequest(R"({"name":"MyLight","scale":{"y":2.0,"z":2.0}})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "scale must be an object with numeric x, y, and z fields");
}

TEST(ParseSetEntityTrsRequestTests, MissingNameFails)
{
    const ParsedSetEntityTrsRequest result = ParseSetEntityTrsRequest(R"({})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: name");
}

TEST(ParseSetEntityTrsRequestTests, MalformedJsonFailsWithExactMessage)
{
    const ParsedSetEntityTrsRequest result = ParseSetEntityTrsRequest("{not valid json");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "malformed JSON body");
}

TEST(ParseSetEntityTrsRequestTests, ExtraUnrecognizedFieldIsIgnored)
{
    const ParsedSetEntityTrsRequest result =
        ParseSetEntityTrsRequest(R"({"name":"MyLight","some_future_field":123})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.name, "MyLight");
}

TEST(ParseSetEntityTrsRequestTests, RotationQuaternionFieldIsSilentlyIgnored)
{
    const ParsedSetEntityTrsRequest result = ParseSetEntityTrsRequest(
        R"({"name":"MyLight","rotation_quaternion":{"x":0,"y":0,"z":0,"w":1}})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FALSE(result.hasRotationEulerDegrees);
}

TEST(ParseInstantiateLightRequestTests, FullyValidPayloadParsesEveryField)
{
    const ParsedInstantiateLightRequest result = ParseInstantiateLightRequest(
        R"({"light_type":"directional","name":"Sun","world_position":{"x":1.0,"y":2.0,"z":3.0},)"
        R"("rotation_euler_degrees":{"x":45.0,"y":-30.0,"z":0.0},)"
        R"("color":{"r":0.5,"g":0.6,"b":0.7},"illuminance_lux":50000.0,"active":false,"parent":"Root"})");

    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.lightType, "directional");
    EXPECT_EQ(result.name, "Sun");
    EXPECT_FLOAT_EQ(result.worldX, 1.0f);
    EXPECT_FLOAT_EQ(result.worldY, 2.0f);
    EXPECT_FLOAT_EQ(result.worldZ, 3.0f);
    ASSERT_TRUE(result.hasRotationEulerDegrees);
    EXPECT_FLOAT_EQ(result.rotationPitchXDegrees, 45.0f);
    EXPECT_FLOAT_EQ(result.rotationYawYDegrees, -30.0f);
    EXPECT_FLOAT_EQ(result.rotationRollZDegrees, 0.0f);
    EXPECT_FLOAT_EQ(result.colorR, 0.5f);
    EXPECT_FLOAT_EQ(result.colorG, 0.6f);
    EXPECT_FLOAT_EQ(result.colorB, 0.7f);
    EXPECT_FLOAT_EQ(result.illuminanceLux, 50000.0f);
    EXPECT_FALSE(result.active);
    EXPECT_TRUE(result.hasParent);
    EXPECT_EQ(result.parentName, "Root");
}

TEST(ParseInstantiateLightRequestTests, MinimalPayloadUsesDocumentedDefaults)
{
    const ParsedInstantiateLightRequest result = ParseInstantiateLightRequest(R"({"name":"Sun"})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.lightType, "");
    EXPECT_FALSE(result.hasRotationEulerDegrees);
    EXPECT_FLOAT_EQ(result.colorR, 1.0f);
    EXPECT_FLOAT_EQ(result.colorG, 1.0f);
    EXPECT_FLOAT_EQ(result.colorB, 1.0f);
    EXPECT_FLOAT_EQ(result.illuminanceLux, 100000.0f);
    EXPECT_TRUE(result.active);
    EXPECT_FALSE(result.hasParent);
}

TEST(ParseInstantiateLightRequestTests, MissingNameFails)
{
    const ParsedInstantiateLightRequest result = ParseInstantiateLightRequest(R"({})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required field: name");
}

TEST(ParseInstantiateLightRequestTests, UnrecognizedLightTypeValueIsNotRejectedHere)
{
    const ParsedInstantiateLightRequest result =
        ParseInstantiateLightRequest(R"({"light_type":"point","name":"Sun"})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.lightType, "point");
}

TEST(ParseInstantiateLightRequestTests, NegativeIlluminanceLuxFails)
{
    const ParsedInstantiateLightRequest result =
        ParseInstantiateLightRequest(R"({"name":"Sun","illuminance_lux":-1.0})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "illuminance_lux must be a non-negative number");
}

TEST(ParseInstantiateLightRequestTests, ActiveNotABooleanFails)
{
    const ParsedInstantiateLightRequest result = ParseInstantiateLightRequest(R"({"name":"Sun","active":"yes"})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "active must be a boolean");
}

TEST(ParseInstantiateLightRequestTests, ColorNotAnObjectFails)
{
    const ParsedInstantiateLightRequest result = ParseInstantiateLightRequest(R"({"name":"Sun","color":"red"})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "color must be an object");
}

TEST(ParseInstantiateLightRequestTests, ColorComponentNotANumberFailsWithComponentSpecificMessage)
{
    const ParsedInstantiateLightRequest result =
        ParseInstantiateLightRequest(R"({"name":"Sun","color":{"r":"nope"}})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "color.r must be a number");
}

TEST(ParseInstantiateLightRequestTests, MalformedJsonFailsWithExactMessage)
{
    const ParsedInstantiateLightRequest result = ParseInstantiateLightRequest("{not valid json");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "malformed JSON body");
}

TEST(BuildSetEntityTrsResponseJsonTests, SuccessShapeRoundTripsWithMixedChangedFlags)
{
    TransformSnapshotView transform;
    transform.positionX = 1.0f;
    transform.positionY = 2.0f;
    transform.positionZ = 3.0f;
    transform.rotationEulerXDegrees = 30.0f;
    transform.rotationEulerYDegrees = 90.0f;
    transform.rotationEulerZDegrees = 0.0f;
    transform.rotationQuatX = 0.1f;
    transform.rotationQuatY = 0.2f;
    transform.rotationQuatZ = 0.3f;
    transform.rotationQuatW = 0.9f;
    transform.scaleX = 1.0f;
    transform.scaleY = 1.0f;
    transform.scaleZ = 1.0f;

    const std::string body = BuildSetEntityTrsResponseJson(
        /*success=*/true, /*errorMessage=*/"", /*entityIndex=*/5, /*entityGeneration=*/1,
        /*translationChanged=*/true, /*rotationChanged=*/false, /*scaleChanged=*/true, transform);

    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["entity"]["index"], 5);
    EXPECT_EQ(parsed["entity"]["generation"], 1);
    EXPECT_EQ(parsed["changed"]["translation"], true);
    EXPECT_EQ(parsed["changed"]["rotation"], false);
    EXPECT_EQ(parsed["changed"]["scale"], true);
    EXPECT_FLOAT_EQ(parsed["transform"]["position"]["x"].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(parsed["transform"]["position"]["y"].get<float>(), 2.0f);
    EXPECT_FLOAT_EQ(parsed["transform"]["position"]["z"].get<float>(), 3.0f);
    EXPECT_FLOAT_EQ(parsed["transform"]["rotation_euler_degrees"]["x"].get<float>(), 30.0f);
    EXPECT_FLOAT_EQ(parsed["transform"]["rotation_euler_degrees"]["y"].get<float>(), 90.0f);
    EXPECT_FLOAT_EQ(parsed["transform"]["rotation_quaternion"]["w"].get<float>(), 0.9f);
    EXPECT_FLOAT_EQ(parsed["transform"]["scale"]["x"].get<float>(), 1.0f);
}

TEST(BuildSetEntityTrsResponseJsonTests, FailureShapeMatchesGenericErrorAndHasNoStrayKeys)
{
    const TransformSnapshotView transform;
    const std::string body = BuildSetEntityTrsResponseJson(
        /*success=*/false, /*errorMessage=*/"entity not found", 0, 0, false, false, false, transform);

    EXPECT_EQ(body, BuildGenericErrorResponseJson("entity not found"));
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_FALSE(parsed.contains("entity"));
    EXPECT_FALSE(parsed.contains("transform"));
    EXPECT_FALSE(parsed.contains("changed"));
}

// --- network-impl-7 campaign
// (PHASE4_HTTP_ENDPOINTS_ACTIVATE_TAB_AND_LIST_TABS.md) -
// GET /activate_tab + GET /list_tabs request parsing/response building.

using gte::Network::BuildActivateTabResponseJson;
using gte::Network::BuildListTabsResponseJson;
using gte::Network::BuildUnknownTabNameResponseJson;
using gte::Network::ParseActivateTabQuery;
using gte::Network::ParsedActivateTabQuery;

TEST(ParseActivateTabQueryTests, RejectsEmptyName)
{
    const ParsedActivateTabQuery result = ParseActivateTabQuery("");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or empty required query parameter: name");
}

TEST(ParseActivateTabQueryTests, AcceptsKnownName)
{
    const ParsedActivateTabQuery result = ParseActivateTabQuery("Profiler");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FALSE(result.notFound);
    EXPECT_EQ(result.tabName, "Profiler");
}

TEST(ParseActivateTabQueryTests, FlagsUnknownNameAsNotFoundNotInvalid)
{
    const ParsedActivateTabQuery result = ParseActivateTabQuery("NotARealTab");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_TRUE(result.notFound);
    EXPECT_EQ(result.tabName, "NotARealTab");
}

TEST(ParseActivateTabQueryTests, IsCaseSensitive)
{
    const ParsedActivateTabQuery lower = ParseActivateTabQuery("profiler");
    EXPECT_TRUE(lower.notFound);
    const ParsedActivateTabQuery upper = ParseActivateTabQuery("PROFILER");
    EXPECT_TRUE(upper.notFound);
}

TEST(BuildActivateTabResponseJsonTests, SuccessShape)
{
    const std::string body = BuildActivateTabResponseJson(true, true, "Profiler");
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["activated_tab"], "Profiler");
    EXPECT_FALSE(parsed.contains("error"));
}

TEST(BuildActivateTabResponseJsonTests, FailureShape)
{
    const std::string body = BuildActivateTabResponseJson(false, false, "Profiler");
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], false);
    EXPECT_EQ(parsed["error"],
        "panel 'Profiler' has no live window yet this session - try again after the Editor has rendered at least one frame");
    EXPECT_FALSE(parsed.contains("activated_tab"));
}

TEST(BuildUnknownTabNameResponseJsonTests, Shape)
{
    const std::string body = BuildUnknownTabNameResponseJson("NotARealTab");
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], false);
    EXPECT_EQ(parsed["error"], "unknown tab name 'NotARealTab' - see GET /list_tabs for the currently known names");
}

// editor-core-separation-3 campaign, PHASE4 - Core/EditorPanelCatalog.h's
// former compile-time-fixed kKnownEditorPanelNames[]/kKnownEditorPanelNameCount
// are gone, replaced by EditorPanelRegistry (a runtime-populated registry,
// seeded for this whole test binary by tests/Core/EditorPanelRegistryTests.cpp's
// own global gtest Environment) - compares against the registry's own live
// state directly instead of a compile-time constant that no longer exists.
TEST(BuildListTabsResponseJsonTests, ContainsEveryRegisteredPanelName)
{
    const std::string body = BuildListTabsResponseJson();
    const nlohmann::json parsed = nlohmann::json::parse(body);
    ASSERT_TRUE(parsed.contains("tabs"));
    const std::vector<std::string>& allNames = gte::EditorPanelRegistry::Instance().AllNames();
    ASSERT_EQ(parsed["tabs"].size(), allNames.size());
    for (std::size_t i = 0; i < allNames.size(); ++i) {
        EXPECT_EQ(parsed["tabs"][i], allNames[i]);
    }
}

// --- task_manager/frame-debugger-3 campaign, PHASE7
// (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) -
// GET /frame_debugger/* request parsing/response building.

using gte::Network::BuildFrameDebuggerCommandResponseJson;
using gte::Network::BuildFrameDebuggerStateResponseJson;
using gte::Network::FrameDebuggerStateResponseView;
using gte::Network::ParseFrameDebuggerEnableQuery;
using gte::Network::ParseFrameDebuggerSelectEventQuery;
using gte::Network::ParseFrameDebuggerSetChannelQuery;
using gte::Network::ParseFrameDebuggerSetLevelsQuery;
using gte::Network::ParsedFrameDebuggerEnableQuery;
using gte::Network::ParsedFrameDebuggerSelectEventQuery;
using gte::Network::ParsedFrameDebuggerSetChannelQuery;
using gte::Network::ParsedFrameDebuggerSetLevelsQuery;

TEST(ParseFrameDebuggerEnableQueryTests, AcceptsTrue)
{
    const ParsedFrameDebuggerEnableQuery result = ParseFrameDebuggerEnableQuery("true");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_TRUE(result.value);
}

TEST(ParseFrameDebuggerEnableQueryTests, AcceptsFalse)
{
    const ParsedFrameDebuggerEnableQuery result = ParseFrameDebuggerEnableQuery("false");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FALSE(result.value);
}

TEST(ParseFrameDebuggerEnableQueryTests, RejectsMissingOrInvalidValue)
{
    const ParsedFrameDebuggerEnableQuery missing = ParseFrameDebuggerEnableQuery("");
    EXPECT_FALSE(missing.valid);
    EXPECT_EQ(missing.errorMessage, "missing or invalid required query parameter: value - must be \"true\" or \"false\"");

    const ParsedFrameDebuggerEnableQuery wrongCase = ParseFrameDebuggerEnableQuery("True");
    EXPECT_FALSE(wrongCase.valid);
}

TEST(ParseFrameDebuggerSelectEventQueryTests, AcceptsNonNegativeInteger)
{
    const ParsedFrameDebuggerSelectEventQuery result = ParseFrameDebuggerSelectEventQuery("5");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.index, 5);
}

TEST(ParseFrameDebuggerSelectEventQueryTests, AcceptsNegativeOneToDeselect)
{
    const ParsedFrameDebuggerSelectEventQuery result = ParseFrameDebuggerSelectEventQuery("-1");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.index, -1);
}

TEST(ParseFrameDebuggerSelectEventQueryTests, RejectsMissingOrNonIntegerIndex)
{
    const ParsedFrameDebuggerSelectEventQuery missing = ParseFrameDebuggerSelectEventQuery("");
    EXPECT_FALSE(missing.valid);
    EXPECT_EQ(missing.errorMessage, "missing or invalid required query parameter: index - must be an integer");

    const ParsedFrameDebuggerSelectEventQuery notAnInt = ParseFrameDebuggerSelectEventQuery("2.5");
    EXPECT_FALSE(notAnInt.valid);

    const ParsedFrameDebuggerSelectEventQuery trailingGarbage = ParseFrameDebuggerSelectEventQuery("3abc");
    EXPECT_FALSE(trailingGarbage.valid);
}

struct ParseFrameDebuggerSetChannelQueryValidCase {
    std::string value;
};

class ParseFrameDebuggerSetChannelQueryValidTest
    : public ::testing::TestWithParam<ParseFrameDebuggerSetChannelQueryValidCase> {};

TEST_P(ParseFrameDebuggerSetChannelQueryValidTest, AcceptsEveryDocumentedChannel)
{
    const ParsedFrameDebuggerSetChannelQuery result = ParseFrameDebuggerSetChannelQuery(GetParam().value);
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.channel, GetParam().value);
}

INSTANTIATE_TEST_SUITE_P(NetworkRoutesTests, ParseFrameDebuggerSetChannelQueryValidTest,
    ::testing::Values(ParseFrameDebuggerSetChannelQueryValidCase{ "all" },
        ParseFrameDebuggerSetChannelQueryValidCase{ "r" }, ParseFrameDebuggerSetChannelQueryValidCase{ "g" },
        ParseFrameDebuggerSetChannelQueryValidCase{ "b" }, ParseFrameDebuggerSetChannelQueryValidCase{ "a" }));

TEST(ParseFrameDebuggerSetChannelQueryTests, RejectsUnknownOrWrongCaseValue)
{
    const ParsedFrameDebuggerSetChannelQuery upper = ParseFrameDebuggerSetChannelQuery("R");
    EXPECT_FALSE(upper.valid);
    EXPECT_EQ(upper.errorMessage, "invalid channel - must be \"all\", \"r\", \"g\", \"b\", or \"a\"");

    // Deliberately never collides with /get_texture's own "color"/"depth"
    // channel values - see this function's own doc comment.
    const ParsedFrameDebuggerSetChannelQuery colorValue = ParseFrameDebuggerSetChannelQuery("color");
    EXPECT_FALSE(colorValue.valid);
}

TEST(ParseFrameDebuggerSetLevelsQueryTests, AcceptsValidFloats)
{
    const ParsedFrameDebuggerSetLevelsQuery result = ParseFrameDebuggerSetLevelsQuery("0.1", "0.9");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_FLOAT_EQ(result.black, 0.1f);
    EXPECT_FLOAT_EQ(result.white, 0.9f);
}

TEST(ParseFrameDebuggerSetLevelsQueryTests, RejectsMissingOrNonNumericBlack)
{
    const ParsedFrameDebuggerSetLevelsQuery missing = ParseFrameDebuggerSetLevelsQuery("", "1.0");
    EXPECT_FALSE(missing.valid);
    EXPECT_EQ(missing.errorMessage, "missing or invalid required query parameter: black - must be a number");

    const ParsedFrameDebuggerSetLevelsQuery nonNumeric = ParseFrameDebuggerSetLevelsQuery("nope", "1.0");
    EXPECT_FALSE(nonNumeric.valid);
}

TEST(ParseFrameDebuggerSetLevelsQueryTests, RejectsMissingOrNonNumericWhite)
{
    const ParsedFrameDebuggerSetLevelsQuery missing = ParseFrameDebuggerSetLevelsQuery("0.0", "");
    EXPECT_FALSE(missing.valid);
    EXPECT_EQ(missing.errorMessage, "missing or invalid required query parameter: white - must be a number");
}

TEST(BuildFrameDebuggerStateResponseJsonTests, ProducesExactExpectedShape)
{
    FrameDebuggerStateResponseView state;
    state.enabled = true;
    state.windowOpen = true;
    state.hasCapturedFrame = true;
    state.totalEventCount = 5;
    state.selectedEventIndex = 4;
    state.channel = "r";
    state.levelsBlack = 0.1f;
    state.levelsWhite = 0.9f;

    const std::string body = BuildFrameDebuggerStateResponseJson(state);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["enabled"], true);
    EXPECT_EQ(parsed["windowOpen"], true);
    EXPECT_EQ(parsed["hasCapturedFrame"], true);
    EXPECT_EQ(parsed["totalEventCount"], 5);
    EXPECT_EQ(parsed["selectedEventIndex"], 4);
    EXPECT_EQ(parsed["channel"], "r");
    EXPECT_FLOAT_EQ(parsed["levelsBlack"].get<float>(), 0.1f);
    EXPECT_FLOAT_EQ(parsed["levelsWhite"].get<float>(), 0.9f);
    // Never wrapped in a "success" envelope - this is a pure status read.
    EXPECT_FALSE(parsed.contains("success"));
}

TEST(BuildFrameDebuggerCommandResponseJsonTests, SuccessShapeHasNoErrorField)
{
    const FrameDebuggerStateResponseView state;
    const std::string body = BuildFrameDebuggerCommandResponseJson(true, "", state);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_FALSE(parsed.contains("error"));
    ASSERT_TRUE(parsed.contains("state"));
    EXPECT_EQ(parsed["state"]["channel"], "all");
}

TEST(BuildFrameDebuggerCommandResponseJsonTests, FailureShapeIncludesErrorAndState)
{
    FrameDebuggerStateResponseView state;
    state.enabled = false;
    const std::string body =
        BuildFrameDebuggerCommandResponseJson(false, "frame debugger command could not be applied", state);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], false);
    EXPECT_EQ(parsed["error"], "frame debugger command could not be applied");
    ASSERT_TRUE(parsed.contains("state"));
    EXPECT_EQ(parsed["state"]["enabled"], false);
}

// --- task_manager/scene-serialization-2 campaign, PHASE5
// (PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md) -
// POST /save_scene + POST /load_scene request parsing/response building.

using gte::Network::BuildScenePathResponseJson;
using gte::Network::ParseScenePathRequest;
using gte::Network::ParsedScenePathRequest;

TEST(ParseScenePathRequestTests, EmptyBodyIsValidWithEmptyPath)
{
    const ParsedScenePathRequest result = ParseScenePathRequest("");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.path, "");
}

TEST(ParseScenePathRequestTests, EmptyObjectIsValidWithEmptyPath)
{
    const ParsedScenePathRequest result = ParseScenePathRequest(R"({})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.path, "");
}

TEST(ParseScenePathRequestTests, ExplicitPathStringIsParsed)
{
    const ParsedScenePathRequest result = ParseScenePathRequest(R"({"path":"C:\\some\\path.gtscene"})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.path, "C:\\some\\path.gtscene");
}

TEST(ParseScenePathRequestTests, ExplicitEmptyStringPathIsValidAndTreatedAsDefault)
{
    const ParsedScenePathRequest result = ParseScenePathRequest(R"({"path":""})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.path, "");
}

TEST(ParseScenePathRequestTests, NullPathIsValidAndTreatedAsDefault)
{
    const ParsedScenePathRequest result = ParseScenePathRequest(R"({"path":null})");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.path, "");
}

TEST(ParseScenePathRequestTests, NonStringPathIsRejected)
{
    const ParsedScenePathRequest result = ParseScenePathRequest(R"({"path":123})");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "path must be a string");
}

TEST(ParseScenePathRequestTests, MalformedJsonBodyIsTreatedAsNoPathNotAnError)
{
    // Deliberately DIFFERENT from every other ParseXxxRequest() in this
    // file - see NetworkRoutes.h's own doc comment for
    // ParsedScenePathRequest.
    const ParsedScenePathRequest result = ParseScenePathRequest("{not valid json");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.path, "");
}

TEST(ParseScenePathRequestTests, NonObjectTopLevelIsTreatedAsNoPathNotAnError)
{
    const ParsedScenePathRequest arrayResult = ParseScenePathRequest("[]");
    ASSERT_TRUE(arrayResult.valid) << arrayResult.errorMessage;
    EXPECT_EQ(arrayResult.path, "");

    const ParsedScenePathRequest numberResult = ParseScenePathRequest("42");
    ASSERT_TRUE(numberResult.valid) << numberResult.errorMessage;
    EXPECT_EQ(numberResult.path, "");
}

TEST(BuildScenePathResponseJsonTests, SuccessShape)
{
    const std::string body = BuildScenePathResponseJson(true, "", "C:\\Project\\TestScene.gtscene");
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["resolved_path"], "C:\\Project\\TestScene.gtscene");
    EXPECT_FALSE(parsed.contains("error"));
}

TEST(BuildScenePathResponseJsonTests, FailureShapeMatchesGenericError)
{
    const std::string body = BuildScenePathResponseJson(false, "boom", "");
    EXPECT_EQ(body, BuildGenericErrorResponseJson("boom"));
}

// --- task_manager/logger-1 campaign, PHASE3
// (PHASE3_NETWORK_ENDPOINTS_GET_LOGS_AND_CLEAR_LOGS.md) -
// GET /get_logs + POST /clear_logs request parsing/response building. See
// this phase's own Step 3.5: the always-compiled cases below never touch
// live Logger:: state (ParseGetLogsQuery()'s own pure parsing logic, and
// BuildGetLogsResponseJson()/BuildClearLogsResponseJson() given a hand-built
// std::vector<LogEntry>) - the block further
// below is what actually calls Logger::Log()/Query()/Clear().

using gte::LogEntry;
using gte::LogLevel;
using gte::Logger;
using gte::Network::BuildClearLogsResponseJson;
using gte::Network::BuildGetLogsResponseJson;
using gte::Network::ParseGetLogsQuery;
using gte::Network::ParsedGetLogsQuery;

TEST(ParseGetLogsQueryTests, AllParamsEmptyProducesDocumentedDefaults)
{
    const ParsedGetLogsQuery result = ParseGetLogsQuery("", "", "", "", "", "", "");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.filter.sinceId, 0u);
    EXPECT_FALSE(result.filter.hasMinLevel);
    EXPECT_EQ(result.filter.category, "");
    EXPECT_EQ(result.filter.keyword, "");
    EXPECT_FALSE(result.filter.hasFrameMin);
    EXPECT_FALSE(result.filter.hasFrameMax);
    EXPECT_EQ(result.filter.limit, 200u);
}

TEST(ParseGetLogsQueryTests, EveryFieldParsesWhenSupplied)
{
    const ParsedGetLogsQuery result =
        ParseGetLogsQuery("42", "warning", "Renderer", "boom", "5", "10", "3");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.filter.sinceId, 42u);
    ASSERT_TRUE(result.filter.hasMinLevel);
    EXPECT_EQ(result.filter.minLevel, LogLevel::Warning);
    EXPECT_EQ(result.filter.category, "Renderer");
    EXPECT_EQ(result.filter.keyword, "boom");
    ASSERT_TRUE(result.filter.hasFrameMin);
    EXPECT_EQ(result.filter.frameMin, 5u);
    ASSERT_TRUE(result.filter.hasFrameMax);
    EXPECT_EQ(result.filter.frameMax, 10u);
    EXPECT_EQ(result.filter.limit, 3u);
}

TEST(ParseGetLogsQueryTests, MinLevelIsCaseInsensitive)
{
    const ParsedGetLogsQuery result = ParseGetLogsQuery("", "ERROR", "", "", "", "", "");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    ASSERT_TRUE(result.filter.hasMinLevel);
    EXPECT_EQ(result.filter.minLevel, LogLevel::Error);
}

TEST(ParseGetLogsQueryTests, RejectsInvalidSinceId)
{
    const ParsedGetLogsQuery negative = ParseGetLogsQuery("-1", "", "", "", "", "", "");
    EXPECT_FALSE(negative.valid);
    EXPECT_EQ(negative.errorMessage, "invalid query parameter: since_id - must be a non-negative integer");

    const ParsedGetLogsQuery garbage = ParseGetLogsQuery("12abc", "", "", "", "", "", "");
    EXPECT_FALSE(garbage.valid);
}

TEST(ParseGetLogsQueryTests, RejectsInvalidMinLevel)
{
    const ParsedGetLogsQuery result = ParseGetLogsQuery("", "bogus", "", "", "", "", "");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage,
        "invalid query parameter: min_level - must be \"debug\", \"info\", \"warning\", or \"error\"");
}

TEST(ParseGetLogsQueryTests, RejectsInvalidFrameMin)
{
    const ParsedGetLogsQuery result = ParseGetLogsQuery("", "", "", "", "nope", "", "");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "invalid query parameter: frame_min - must be a non-negative integer");
}

TEST(ParseGetLogsQueryTests, RejectsInvalidFrameMax)
{
    const ParsedGetLogsQuery result = ParseGetLogsQuery("", "", "", "", "", "-3", "");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "invalid query parameter: frame_max - must be a non-negative integer");
}

TEST(ParseGetLogsQueryTests, RejectsInvalidLimit)
{
    const ParsedGetLogsQuery result = ParseGetLogsQuery("", "", "", "", "", "", "abc");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "invalid query parameter: limit - must be a non-negative integer");
}

TEST(ParseGetLogsQueryTests, LimitLargerThanCapacityIsClampedNotRejected)
{
    const ParsedGetLogsQuery result = ParseGetLogsQuery("", "", "", "", "", "", "999999999");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.filter.limit, Logger::kCapacity);
}

TEST(ParseGetLogsQueryTests, CategoryAndKeywordAreAlwaysValidAsIs)
{
    const ParsedGetLogsQuery result = ParseGetLogsQuery("", "", "Anything Goes", "any keyword too", "", "", "");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.filter.category, "Anything Goes");
    EXPECT_EQ(result.filter.keyword, "any keyword too");
}

TEST(BuildGetLogsResponseJsonTests, EmptyEntriesProducesEmptyArrayShape)
{
    const std::string body = BuildGetLogsResponseJson({}, true, 0);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["logging_enabled"], true);
    EXPECT_EQ(parsed["count"], 0);
    EXPECT_EQ(parsed["latest_id"], 0);
    ASSERT_TRUE(parsed.contains("entries"));
    EXPECT_TRUE(parsed["entries"].empty());
}

TEST(BuildGetLogsResponseJsonTests, PopulatedEntriesRoundTripEveryField)
{
    LogEntry entry;
    entry.id = 7;
    entry.frameNumber = 3;
    entry.timestampSeconds = 1.5;
    entry.level = LogLevel::Warning;
    entry.category = "Renderer";
    entry.message = "something happened";

    const std::vector<LogEntry> entries = { entry };
    const std::string body = BuildGetLogsResponseJson(entries, false, 7);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["logging_enabled"], false);
    EXPECT_EQ(parsed["count"], 1);
    EXPECT_EQ(parsed["latest_id"], 7);
    ASSERT_EQ(parsed["entries"].size(), 1u);
    EXPECT_EQ(parsed["entries"][0]["id"], 7);
    EXPECT_EQ(parsed["entries"][0]["frame"], 3);
    EXPECT_DOUBLE_EQ(parsed["entries"][0]["timestamp_seconds"].get<double>(), 1.5);
    EXPECT_EQ(parsed["entries"][0]["level"], "Warning");
    EXPECT_EQ(parsed["entries"][0]["category"], "Renderer");
    EXPECT_EQ(parsed["entries"][0]["message"], "something happened");
}

TEST(BuildClearLogsResponseJsonTests, ProducesExpectedShape)
{
    const std::string body = BuildClearLogsResponseJson(5);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["cleared_count"], 5);
}

// These cases actually call gte::Logger::Log()/Query()/Clear() - always
// compiled in since editor-core-separation-1's PHASE8 (GTE_ENABLE_EDITOR no
// longer exists anywhere in this codebase; Logger::Query() always returns
// real, live results now).
TEST(GetLogsEndToEndTests, ParseAndQueryAndBuildRoundTripRealLoggerState)
{
    Logger::Clear();
    GTE_LOG_INFO("Renderer", "first entry");
    GTE_LOG_WARNING("Jobs", "second entry");
    GTE_LOG_ERROR("Renderer", "third entry");

    const ParsedGetLogsQuery parsed = ParseGetLogsQuery("", "", "", "", "", "", "");
    ASSERT_TRUE(parsed.valid) << parsed.errorMessage;

    const std::vector<LogEntry> entries = Logger::Query(parsed.filter);
    ASSERT_EQ(entries.size(), 3u);

    const std::string body = BuildGetLogsResponseJson(entries, Logger::IsEnabled(), Logger::LatestEntryId());
    const nlohmann::json responseJson = nlohmann::json::parse(body);
    EXPECT_EQ(responseJson["logging_enabled"], true);
    EXPECT_EQ(responseJson["count"], 3);
    ASSERT_EQ(responseJson["entries"].size(), 3u);
    EXPECT_EQ(responseJson["entries"][0]["message"], "first entry");
    EXPECT_EQ(responseJson["entries"][1]["message"], "second entry");
    EXPECT_EQ(responseJson["entries"][2]["message"], "third entry");

    Logger::Clear();
}

TEST(GetLogsEndToEndTests, CategoryFilterAppliesThroughParseAndQuery)
{
    Logger::Clear();
    GTE_LOG_INFO("Renderer", "renderer entry");
    GTE_LOG_INFO("Jobs", "jobs entry");

    const ParsedGetLogsQuery parsed = ParseGetLogsQuery("", "", "Jobs", "", "", "", "");
    ASSERT_TRUE(parsed.valid) << parsed.errorMessage;

    const std::vector<LogEntry> entries = Logger::Query(parsed.filter);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].category, "Jobs");
    EXPECT_EQ(entries[0].message, "jobs entry");

    Logger::Clear();
}

TEST(GetLogsEndToEndTests, SinceIdCursorAppliesThroughParseAndQuery)
{
    Logger::Clear();
    GTE_LOG_INFO("Renderer", "entry one");
    const std::uint64_t cursor = Logger::LatestEntryId();
    GTE_LOG_INFO("Renderer", "entry two");

    const ParsedGetLogsQuery parsed = ParseGetLogsQuery(std::to_string(cursor), "", "", "", "", "", "");
    ASSERT_TRUE(parsed.valid) << parsed.errorMessage;

    const std::vector<LogEntry> entries = Logger::Query(parsed.filter);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].message, "entry two");

    Logger::Clear();
}

TEST(ClearLogsEndToEndTests, ClearEmptiesBufferAndReportsPreviousCount)
{
    Logger::Clear();
    GTE_LOG_INFO("Renderer", "one");
    GTE_LOG_INFO("Renderer", "two");
    ASSERT_EQ(Logger::EntryCount(), 2u);

    const std::size_t clearedCount = Logger::EntryCount();
    Logger::Clear();
    const std::string body = BuildClearLogsResponseJson(clearedCount);
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["cleared_count"], 2);
    EXPECT_EQ(Logger::EntryCount(), 0u);
}

// GET /render_graph/set_pass_enabled, /passes, /set_feature_enabled,
// /set_feature_priority request parsing/response building.

using gte::Network::BuildRenderGraphControlCommandResponseJson;
using gte::Network::BuildRenderGraphControlPassStatesResponseJson;
using gte::Network::ParseRenderGraphSetFeatureEnabledQuery;
using gte::Network::ParseRenderGraphSetFeaturePriorityQuery;
using gte::Network::ParseRenderGraphSetPassEnabledQuery;
using gte::Network::ParsedRenderGraphSetFeatureEnabledQuery;
using gte::Network::ParsedRenderGraphSetFeaturePriorityQuery;
using gte::Network::ParsedRenderGraphSetPassEnabledQuery;
using gte::Network::RenderGraphControlPassStateResponseView;

TEST(ParseRenderGraphSetPassEnabledQueryTests, AcceptsValidNameAndBool)
{
    const ParsedRenderGraphSetPassEnabledQuery trueCase = ParseRenderGraphSetPassEnabledQuery("RenderOpaque", "true");
    ASSERT_TRUE(trueCase.valid) << trueCase.errorMessage;
    EXPECT_EQ(trueCase.name, "RenderOpaque");
    EXPECT_TRUE(trueCase.enabled);

    const ParsedRenderGraphSetPassEnabledQuery falseCase = ParseRenderGraphSetPassEnabledQuery("RenderOpaque", "false");
    ASSERT_TRUE(falseCase.valid) << falseCase.errorMessage;
    EXPECT_FALSE(falseCase.enabled);
}

TEST(ParseRenderGraphSetPassEnabledQueryTests, RejectsEmptyName)
{
    const ParsedRenderGraphSetPassEnabledQuery result = ParseRenderGraphSetPassEnabledQuery("", "true");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorMessage, "missing or invalid required query parameter: name - must be non-empty");
}

TEST(ParseRenderGraphSetPassEnabledQueryTests, RejectsMissingOrInvalidEnabled)
{
    const ParsedRenderGraphSetPassEnabledQuery missing = ParseRenderGraphSetPassEnabledQuery("RenderOpaque", "");
    EXPECT_FALSE(missing.valid);
    EXPECT_EQ(missing.errorMessage, "missing or invalid required query parameter: enabled - must be \"true\" or \"false\"");

    const ParsedRenderGraphSetPassEnabledQuery wrongCase = ParseRenderGraphSetPassEnabledQuery("RenderOpaque", "True");
    EXPECT_FALSE(wrongCase.valid);
}

TEST(ParseRenderGraphSetFeatureEnabledQueryTests, AcceptsValidNameAndBool)
{
    const ParsedRenderGraphSetFeatureEnabledQuery result = ParseRenderGraphSetFeatureEnabledQuery("DemoRenderFeatureV2", "false");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.name, "DemoRenderFeatureV2");
    EXPECT_FALSE(result.enabled);
}

TEST(ParseRenderGraphSetFeatureEnabledQueryTests, RejectsEmptyNameOrInvalidEnabled)
{
    const ParsedRenderGraphSetFeatureEnabledQuery emptyName = ParseRenderGraphSetFeatureEnabledQuery("", "true");
    EXPECT_FALSE(emptyName.valid);
    EXPECT_EQ(emptyName.errorMessage, "missing or invalid required query parameter: name - must be non-empty");

    const ParsedRenderGraphSetFeatureEnabledQuery badBool = ParseRenderGraphSetFeatureEnabledQuery("Demo", "nope");
    EXPECT_FALSE(badBool.valid);
    EXPECT_EQ(badBool.errorMessage, "missing or invalid required query parameter: enabled - must be \"true\" or \"false\"");
}

TEST(ParseRenderGraphSetFeaturePriorityQueryTests, AcceptsValidNameAndInteger)
{
    const ParsedRenderGraphSetFeaturePriorityQuery result = ParseRenderGraphSetFeaturePriorityQuery("Demo", "-3");
    ASSERT_TRUE(result.valid) << result.errorMessage;
    EXPECT_EQ(result.name, "Demo");
    EXPECT_EQ(result.priority, -3);
}

TEST(ParseRenderGraphSetFeaturePriorityQueryTests, RejectsEmptyNameOrNonIntegerPriority)
{
    const ParsedRenderGraphSetFeaturePriorityQuery emptyName = ParseRenderGraphSetFeaturePriorityQuery("", "3");
    EXPECT_FALSE(emptyName.valid);
    EXPECT_EQ(emptyName.errorMessage, "missing or invalid required query parameter: name - must be non-empty");

    const ParsedRenderGraphSetFeaturePriorityQuery notAnInt = ParseRenderGraphSetFeaturePriorityQuery("Demo", "abc");
    EXPECT_FALSE(notAnInt.valid);
    EXPECT_EQ(notAnInt.errorMessage, "missing or invalid required query parameter: priority - must be an integer");

    const ParsedRenderGraphSetFeaturePriorityQuery trailingGarbage = ParseRenderGraphSetFeaturePriorityQuery("Demo", "3abc");
    EXPECT_FALSE(trailingGarbage.valid);
}

TEST(BuildRenderGraphControlCommandResponseJsonTests, SuccessShapeHasNoErrorField)
{
    const std::string body = BuildRenderGraphControlCommandResponseJson(true, "");
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_FALSE(parsed.contains("error"));
}

TEST(BuildRenderGraphControlCommandResponseJsonTests, FailureShapeIncludesError)
{
    const std::string body = BuildRenderGraphControlCommandResponseJson(false, "\"Present\" cannot be disabled (deny-listed).");
    const nlohmann::json parsed = nlohmann::json::parse(body);
    EXPECT_EQ(parsed["success"], false);
    EXPECT_EQ(parsed["error"], "\"Present\" cannot be disabled (deny-listed).");
}

TEST(BuildRenderGraphControlPassStatesResponseJsonTests, ProducesExpectedShapeForEmptyAndNonEmptyLists)
{
    const std::string emptyBody = BuildRenderGraphControlPassStatesResponseJson({});
    const nlohmann::json emptyParsed = nlohmann::json::parse(emptyBody);
    ASSERT_TRUE(emptyParsed.contains("passes"));
    EXPECT_TRUE(emptyParsed["passes"].is_array());
    EXPECT_EQ(emptyParsed["passes"].size(), 0u);

    RenderGraphControlPassStateResponseView enabledPass;
    enabledPass.name = "RenderOpaque";
    enabledPass.enabled = true;
    enabledPass.everDeclaredThisSession = true;

    RenderGraphControlPassStateResponseView neverDeclaredPass;
    neverDeclaredPass.name = "SomePass";
    neverDeclaredPass.enabled = false;
    neverDeclaredPass.everDeclaredThisSession = false;

    const std::string body =
        BuildRenderGraphControlPassStatesResponseJson({ enabledPass, neverDeclaredPass });
    const nlohmann::json parsed = nlohmann::json::parse(body);
    ASSERT_TRUE(parsed.contains("passes"));
    ASSERT_EQ(parsed["passes"].size(), 2u);
    EXPECT_EQ(parsed["passes"][0]["name"], "RenderOpaque");
    EXPECT_EQ(parsed["passes"][0]["enabled"], true);
    EXPECT_EQ(parsed["passes"][0]["ever_declared_this_session"], true);
    EXPECT_EQ(parsed["passes"][1]["name"], "SomePass");
    EXPECT_EQ(parsed["passes"][1]["enabled"], false);
    EXPECT_EQ(parsed["passes"][1]["ever_declared_this_session"], false);
}

} // namespace
