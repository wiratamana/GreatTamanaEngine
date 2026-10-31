// Tier 1 tests for the Scene camera routes: pure parsing and JSON building only.

#include "Network/NetworkRoutes.h"
#include "Network/NetworkSceneCameraPose.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

using namespace gte;
using namespace gte::Network;

TEST(SceneCameraImageQueryTests, OverlayDefaultsToTrue)
{
    const ParsedSceneCameraImageQuery defaults = ParseSceneCameraImageQuery("");
    ASSERT_TRUE(defaults.valid);
    EXPECT_TRUE(defaults.overlay);

    const ParsedSceneCameraImageQuery off = ParseSceneCameraImageQuery("false");
    ASSERT_TRUE(off.valid);
    EXPECT_FALSE(off.overlay);

    EXPECT_FALSE(ParseSceneCameraImageQuery("maybe").valid);
}

TEST(SceneCameraStatusTests, ErrorsMapToHttpStatus)
{
    EXPECT_EQ(HttpStatusForError(SceneCameraError::BadInput), 400);
    EXPECT_EQ(HttpStatusForError(SceneCameraError::NotFound), 404);
    EXPECT_EQ(HttpStatusForError(SceneCameraError::NotRendered), 409);
    EXPECT_EQ(HttpStatusForError(SceneCameraError::NoCamera), 409);
    EXPECT_EQ(HttpStatusForError(SceneCameraError::Unavailable), 503);
}

TEST(SceneCameraStateJsonTests, ListsCamerasAndTheSelectedFrustum)
{
    SceneCameraReport report;
    report.rendered = true;
    report.imageWidth = 800;
    report.imageHeight = 600;
    report.selectedEntity = 7;

    SceneCameraInfo camera;
    camera.entityId = 7;
    camera.name = "Main Camera";
    camera.position[1] = 2.0f;
    camera.fovYDegrees = 60.0f;
    camera.selected = true;
    camera.apexScreenX = 0.5f;
    camera.apexScreenY = 0.25f;
    camera.apexVisible = true;
    report.cameras.push_back(camera);

    OverlaySegment2D segment;
    segment.a = Vec2(1.0f, 2.0f);
    segment.b = Vec2(3.0f, 4.0f);
    report.selectedFrustum.push_back(segment);

    const nlohmann::json json = nlohmann::json::parse(BuildSceneCameraStateJson(report));
    EXPECT_TRUE(json["rendered"].get<bool>());
    EXPECT_EQ(json["image"]["width"].get<int>(), 800);
    EXPECT_EQ(json["selected_entity"].get<int>(), 7);
    ASSERT_EQ(json["cameras"].size(), 1u);
    EXPECT_EQ(json["cameras"][0]["name"].get<std::string>(), "Main Camera");
    EXPECT_FLOAT_EQ(json["cameras"][0]["position"]["y"].get<float>(), 2.0f);
    EXPECT_TRUE(json["cameras"][0]["screen"]["visible"].get<bool>());
    ASSERT_EQ(json["selected_frustum"].size(), 1u);
    EXPECT_FLOAT_EQ(json["selected_frustum"][0]["b"]["y"].get<float>(), 4.0f);
}

TEST(SceneCameraStateJsonTests, EmptyReportHasEmptyArrays)
{
    const nlohmann::json json = nlohmann::json::parse(BuildSceneCameraStateJson({}));
    EXPECT_FALSE(json["rendered"].get<bool>());
    EXPECT_TRUE(json["cameras"].empty());
    EXPECT_TRUE(json["selected_frustum"].empty());
}

TEST(SceneCameraPickResponseTests, HitCarriesTheEntityName)
{
    const nlohmann::json hit = nlohmann::json::parse(BuildPickResponseJson(true, "Main Camera"));
    EXPECT_TRUE(hit["hit"].get<bool>());
    EXPECT_EQ(hit["entity"].get<std::string>(), "Main Camera");

    const nlohmann::json miss = nlohmann::json::parse(BuildPickResponseJson(false, ""));
    EXPECT_FALSE(miss["hit"].get<bool>());
    EXPECT_FALSE(miss.contains("entity"));
}

TEST(SceneCameraSelectResponseTests, EchoesTheName)
{
    const nlohmann::json json = nlohmann::json::parse(BuildSceneCameraSelectResponseJson("Camera"));
    EXPECT_TRUE(json["success"].get<bool>());
    EXPECT_EQ(json["selected"].get<std::string>(), "Camera");
}

TEST(SceneCameraStateJsonTests, HasNoCullViewKey)
{
    const nlohmann::json json = nlohmann::json::parse(BuildSceneCameraStateJson({}));
    EXPECT_FALSE(json.contains("cull_view"));
}

TEST(SceneCameraPoseJsonTests, CarriesPositionYawAndPitch)
{
    SceneCameraReport report;
    report.pose.position[0] = 1.5f;
    report.pose.position[1] = 2.0f;
    report.pose.position[2] = -12.0f;
    report.pose.yawDegrees = 90.0f;
    report.pose.pitchDegrees = 10.0f;

    const nlohmann::json json = nlohmann::json::parse(BuildSceneCameraPoseJson(report));
    EXPECT_TRUE(json["success"].get<bool>());
    EXPECT_FLOAT_EQ(json["position"]["x"].get<float>(), 1.5f);
    EXPECT_FLOAT_EQ(json["position"]["y"].get<float>(), 2.0f);
    EXPECT_FLOAT_EQ(json["position"]["z"].get<float>(), -12.0f);
    EXPECT_FLOAT_EQ(json["yaw_degrees"].get<float>(), 90.0f);
    EXPECT_FLOAT_EQ(json["pitch_degrees"].get<float>(), 10.0f);
}

TEST(SceneCameraPoseJsonTests, EmbeddedObjectHasNoSuccessKey)
{
    const nlohmann::json json = ScenePoseToJson(SceneCameraPose{});
    EXPECT_FALSE(json.contains("success"));
    EXPECT_TRUE(json.contains("position"));
    EXPECT_TRUE(json.contains("yaw_degrees"));
    EXPECT_TRUE(json.contains("pitch_degrees"));
}

TEST(SceneCameraSetPoseQueryTests, ParsesFiveValidValues)
{
    const ParsedSetPoseQuery parsed = ParseSetPoseQuery("0", "2", "-12", "0", "10.5");
    ASSERT_TRUE(parsed.valid);
    EXPECT_FLOAT_EQ(parsed.y, 2.0f);
    EXPECT_FLOAT_EQ(parsed.z, -12.0f);
    EXPECT_FLOAT_EQ(parsed.pitch, 10.5f);
}

TEST(SceneCameraSetPoseQueryTests, MissingKeyNamesTheKey)
{
    const ParsedSetPoseQuery parsed = ParseSetPoseQuery("0", "2", "", "0", "0");
    EXPECT_FALSE(parsed.valid);
    EXPECT_NE(parsed.errorMessage.find(": z "), std::string::npos);
}

TEST(SceneCameraSetPoseQueryTests, RejectsNonFiniteAndHugeValues)
{
    EXPECT_FALSE(ParseSetPoseQuery("nan", "0", "0", "0", "0").valid);
    EXPECT_FALSE(ParseSetPoseQuery("0", "inf", "0", "0", "0").valid);
    EXPECT_FALSE(ParseSetPoseQuery("0", "0", "-inf", "0", "0").valid);
    EXPECT_FALSE(ParseSetPoseQuery("1e9", "0", "0", "0", "0").valid);
    EXPECT_FALSE(ParseSetPoseQuery("0", "0", "0", "1e9", "0").valid);
}

TEST(SceneCameraSetPoseQueryTests, RejectsTrailingTextAndNamesYaw)
{
    EXPECT_FALSE(ParseSetPoseQuery("1abc", "0", "0", "0", "0").valid);
    EXPECT_FALSE(ParseSetPoseQuery(" 1", "0", "0", "0", "0").valid);

    const ParsedSetPoseQuery parsed = ParseSetPoseQuery("0", "0", "0", "left", "0");
    EXPECT_FALSE(parsed.valid);
    EXPECT_NE(parsed.errorMessage.find(": yaw "), std::string::npos);
}

TEST(SceneCameraExitReplyTests, SaysExitRequested)
{
    const nlohmann::json body = nlohmann::json::parse(BuildExitReplyJson());
    EXPECT_TRUE(body.at("success").get<bool>());
    EXPECT_EQ(body.at("message").get<std::string>(), "exit requested");
}

TEST(SceneCameraStateJsonTests, CarriesThePose)
{
    SceneCameraReport report;
    report.pose.position[2] = -12.0f;
    report.pose.pitchDegrees = 10.0f;
    const nlohmann::json body = nlohmann::json::parse(BuildSceneCameraStateJson(report));
    EXPECT_FLOAT_EQ(body.at("pose").at("position").at("z").get<float>(), -12.0f);
    EXPECT_FLOAT_EQ(body.at("pose").at("pitch_degrees").get<float>(), 10.0f);
    EXPECT_FALSE(body.at("pose").contains("success"));
}

TEST(SceneCameraSetViewQueryTests, ParsesEveryPart)
{
    const ParsedSetViewQuery parsed = ParseSetViewQuery("top", "orthographic", "12.5");
    ASSERT_TRUE(parsed.valid);
    ASSERT_TRUE(parsed.request.axis.has_value());
    EXPECT_EQ(*parsed.request.axis, SceneViewAxis::PositiveY);
    ASSERT_TRUE(parsed.request.orthographic.has_value());
    EXPECT_TRUE(*parsed.request.orthographic);
    ASSERT_TRUE(parsed.request.distance.has_value());
    EXPECT_FLOAT_EQ(*parsed.request.distance, 12.5f);
}

TEST(SceneCameraSetViewQueryTests, ParsesOnePartAlone)
{
    const ParsedSetViewQuery axisOnly = ParseSetViewQuery("front", "", "");
    ASSERT_TRUE(axisOnly.valid);
    EXPECT_TRUE(axisOnly.request.axis.has_value());
    EXPECT_FALSE(axisOnly.request.orthographic.has_value());
    EXPECT_FALSE(axisOnly.request.distance.has_value());

    const ParsedSetViewQuery projectionOnly = ParseSetViewQuery("", "perspective", "");
    ASSERT_TRUE(projectionOnly.valid);
    EXPECT_FALSE(projectionOnly.request.axis.has_value());
    ASSERT_TRUE(projectionOnly.request.orthographic.has_value());
    EXPECT_FALSE(*projectionOnly.request.orthographic);
    EXPECT_FALSE(projectionOnly.request.distance.has_value());

    const ParsedSetViewQuery distanceOnly = ParseSetViewQuery("", "", "3");
    ASSERT_TRUE(distanceOnly.valid);
    EXPECT_FALSE(distanceOnly.request.axis.has_value());
    EXPECT_FALSE(distanceOnly.request.orthographic.has_value());
    ASSERT_TRUE(distanceOnly.request.distance.has_value());
    EXPECT_FLOAT_EQ(*distanceOnly.request.distance, 3.0f);
}

TEST(SceneCameraSetViewQueryTests, RejectsEmpty)
{
    const ParsedSetViewQuery parsed = ParseSetViewQuery("", "", "");
    EXPECT_FALSE(parsed.valid);
    EXPECT_NE(parsed.errorMessage.find("at least one"), std::string::npos);
}

TEST(SceneCameraSetViewQueryTests, NamesTheBadKey)
{
    EXPECT_NE(ParseSetViewQuery("up", "", "").errorMessage.find("direction"), std::string::npos);
    EXPECT_NE(ParseSetViewQuery("", "round", "").errorMessage.find("projection"), std::string::npos);
    EXPECT_NE(ParseSetViewQuery("", "", "abc").errorMessage.find("distance"), std::string::npos);

    EXPECT_FALSE(ParseSetViewQuery("top", "", "0.01").valid);
    EXPECT_FALSE(ParseSetViewQuery("top", "", "1e9").valid);
    EXPECT_FALSE(ParseSetViewQuery("top", "", "nan").valid);
    EXPECT_TRUE(ParseSetViewQuery("top", "", "").valid);
}

TEST(SceneCameraSetViewQueryTests, RejectsUppercaseDirection)
{
    EXPECT_FALSE(ParseSetViewQuery("Top", "", "").valid);
}

TEST(SceneCameraViewJsonTests, PoseJsonCarriesTheView)
{
    SceneCameraReport report;
    report.view.distance = 7.5f;
    report.view.orthographic = true;
    report.view.axisName = "Top";

    const nlohmann::json json = nlohmann::json::parse(BuildSceneCameraPoseJson(report));
    EXPECT_TRUE(json["success"].get<bool>());
    EXPECT_EQ(json["view"]["projection"].get<std::string>(), "orthographic");
    EXPECT_FLOAT_EQ(json["view"]["distance"].get<float>(), 7.5f);
    EXPECT_EQ(json["view"]["axis"].get<std::string>(), "Top");
}

TEST(SceneCameraViewJsonTests, StateJsonCarriesTheViewAndTheDefaultIsPerspective)
{
    const nlohmann::json json = nlohmann::json::parse(BuildSceneCameraStateJson({}));
    EXPECT_EQ(json["view"]["projection"].get<std::string>(), "perspective");
    EXPECT_EQ(json["view"]["axis"].get<std::string>(), "");
}

TEST(SceneCameraViewJsonTests, EmbeddedViewHasNoSuccessKey)
{
    const nlohmann::json json = SceneViewToJson(SceneCameraView{});
    EXPECT_FALSE(json.contains("success"));
    EXPECT_TRUE(json.contains("distance"));
}

} // namespace
