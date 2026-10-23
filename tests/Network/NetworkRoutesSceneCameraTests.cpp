// Tier 1 tests for the Scene camera routes: pure parsing and JSON building only.

#include "Network/NetworkRoutes.h"

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

TEST(SceneCameraStateJsonTests, CullViewIsMarkedInvalidWithoutAReport)
{
    const nlohmann::json json = nlohmann::json::parse(BuildSceneCameraStateJson({}));
    ASSERT_TRUE(json.contains("cull_view"));
    EXPECT_FALSE(json["cull_view"]["valid"].get<bool>());
    EXPECT_FALSE(json["cull_view"].contains("visible"));
}

TEST(SceneCameraStateJsonTests, CullViewCarriesTheHudNumbers)
{
    SceneCameraReport report;
    report.cullView.valid = true;
    report.cullView.rendered = true;
    report.cullView.mode = "Frustum";
    report.cullView.testedAtVisibleFrame = 100;
    report.cullView.visible = 60;
    report.cullView.hidden = 40;
    report.cullView.visibleValid = true;
    report.cullView.frozen = true;
    report.cullView.frozenFrame = 12;

    const nlohmann::json json = nlohmann::json::parse(BuildSceneCameraStateJson(report));
    const nlohmann::json& cull = json["cull_view"];
    EXPECT_TRUE(cull["valid"].get<bool>());
    EXPECT_EQ(cull["mode"].get<std::string>(), "Frustum");
    EXPECT_EQ(cull["visible"].get<int>(), 60);
    EXPECT_EQ(cull["hidden"].get<int>(), 40);
    EXPECT_EQ(cull["tested_at_visible_frame"].get<int>(), 100);
    EXPECT_TRUE(cull["counts_valid"].get<bool>());
    EXPECT_EQ(cull["frozen_frame"].get<int>(), 12);
}

} // namespace
