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

} // namespace
