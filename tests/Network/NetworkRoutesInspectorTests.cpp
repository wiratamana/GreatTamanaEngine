// Tier 1 tests for the Inspector and split-view routes: pure parsing and JSON building only.

#include "Network/NetworkRoutes.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

using namespace gte::Network;

TEST(ParseSelectAssetQueryTests, AbsoluteGtaPathIsAccepted)
{
    const ParsedSelectAssetQuery drive = ParseSelectAssetQuery("C:\\Project\\Meshes\\rock.gta");
    ASSERT_TRUE(drive.valid) << drive.errorMessage;
    EXPECT_EQ(drive.path, "C:\\Project\\Meshes\\rock.gta");

    EXPECT_TRUE(ParseSelectAssetQuery("D:/Project/rock.GTA").valid);
    EXPECT_TRUE(ParseSelectAssetQuery("/data/Project/rock.gta").valid);
}

TEST(ParseSelectAssetQueryTests, EmptyRelativeOrWrongExtensionIsRefused)
{
    EXPECT_FALSE(ParseSelectAssetQuery("").valid);
    EXPECT_FALSE(ParseSelectAssetQuery("Meshes\\rock.gta").valid);
    EXPECT_FALSE(ParseSelectAssetQuery("C:\\Project\\rock.stl").valid);
    EXPECT_FALSE(ParseSelectAssetQuery("C:\\Project\\rock").valid);
    EXPECT_FALSE(ParseSelectAssetQuery("C:rock.gta").valid);
}

TEST(ParseSplitViewsQueryTests, OnlyExactTrueAndFalseAreAccepted)
{
    const ParsedSplitViewsQuery on = ParseSplitViewsQuery("true");
    ASSERT_TRUE(on.valid);
    EXPECT_TRUE(on.enabled);

    const ParsedSplitViewsQuery off = ParseSplitViewsQuery("false");
    ASSERT_TRUE(off.valid);
    EXPECT_FALSE(off.enabled);

    EXPECT_FALSE(ParseSplitViewsQuery("").valid);
    EXPECT_FALSE(ParseSplitViewsQuery("TRUE").valid);
    EXPECT_FALSE(ParseSplitViewsQuery("1").valid);
}

TEST(ParseInspectorBuildClustersRequestTests, BlankBodyKeepsDefaults)
{
    const ParsedBuildClustersRequest parsed = ParseInspectorBuildClustersRequest("  \n");
    ASSERT_TRUE(parsed.valid) << parsed.errorMessage;
    EXPECT_EQ(parsed.weld, "exact");
    EXPECT_EQ(parsed.maxVertices, 64u);
    EXPECT_EQ(parsed.maxTriangles, 124u);
    EXPECT_TRUE(parsed.gtaPath.empty());
}

TEST(ParseInspectorBuildClustersRequestTests, WeldAndLimitsAreRead)
{
    const ParsedBuildClustersRequest parsed = ParseInspectorBuildClustersRequest(
        R"({"weld":"position_only","max_vertices":32,"max_triangles":64,"cone_weight":0.25})");
    ASSERT_TRUE(parsed.valid) << parsed.errorMessage;
    EXPECT_EQ(parsed.weld, "position_only");
    EXPECT_EQ(parsed.maxVertices, 32u);
    EXPECT_EQ(parsed.maxTriangles, 64u);
    EXPECT_FLOAT_EQ(parsed.coneWeight, 0.25f);
}

TEST(ParseInspectorBuildClustersRequestTests, SameValidationAsTheDirectRoute)
{
    EXPECT_FALSE(ParseInspectorBuildClustersRequest("{").valid);
    EXPECT_FALSE(ParseInspectorBuildClustersRequest("[]").valid);
    EXPECT_FALSE(ParseInspectorBuildClustersRequest(R"({"weld":"loose"})").valid);
    EXPECT_FALSE(ParseInspectorBuildClustersRequest(R"({"max_vertices":2})").valid);
    EXPECT_FALSE(ParseInspectorBuildClustersRequest(R"({"max_triangles":10})").valid);
    EXPECT_FALSE(ParseInspectorBuildClustersRequest(R"({"cone_weight":1.5})").valid);
    EXPECT_FALSE(ParseInspectorBuildClustersRequest(R"({"max_vertices":"64"})").valid);
}

TEST(ParseBuildClustersRequestTests, StillRequiresThePath)
{
    EXPECT_FALSE(ParseBuildClustersRequest(R"({"weld":"exact"})").valid);
    const ParsedBuildClustersRequest parsed = ParseBuildClustersRequest(R"({"gta_path":"C:\\a.gta","weld":"exact"})");
    ASSERT_TRUE(parsed.valid) << parsed.errorMessage;
    EXPECT_EQ(parsed.gtaPath, "C:\\a.gta");
}

TEST(InspectorResponseTests, StateJsonCarriesEveryField)
{
    InspectorStateResponseView view;
    view.selectionKind = "asset";
    view.selectedPath = "C:\\P\\a.gta";
    view.shownPath = "C:\\P\\a.gta";
    view.clustersText = "Clusters: 12";
    view.clusterStatus = "12 clusters";
    view.buildEnabled = false;
    view.buildDisabledReason = "A cluster build is already running.";
    view.buildRunning = true;

    const nlohmann::json body = nlohmann::json::parse(BuildInspectorStateResponseJson(view));
    EXPECT_EQ(body["selection_kind"], "asset");
    EXPECT_EQ(body["selected_path"], "C:\\P\\a.gta");
    EXPECT_EQ(body["shown_path"], "C:\\P\\a.gta");
    EXPECT_EQ(body["clusters_text"], "Clusters: 12");
    EXPECT_EQ(body["cluster_status"], "12 clusters");
    EXPECT_FALSE(body["build_button_enabled"].get<bool>());
    EXPECT_EQ(body["build_button_disabled_reason"], "A cluster build is already running.");
    EXPECT_TRUE(body["build_running"].get<bool>());
}

TEST(InspectorResponseTests, StateJsonCarriesRotationField)
{
    InspectorStateResponseView view;
    view.rotation.drawn = true;
    view.rotation.entity = gte::Entity{ 3, 2 };
    view.rotation.shown = gte::Vec3(9999.0f, 0.0f, 0.0f);
    view.rotation.axisCenter[1] = gte::Vec2(120.0f, 40.0f);
    view.rotation.rederiveCount = 7;

    const nlohmann::json rotation = nlohmann::json::parse(BuildInspectorStateResponseJson(view))["rotation"];
    EXPECT_TRUE(rotation["drawn"].get<bool>());
    EXPECT_EQ(rotation["entity"]["index"], 3u);
    EXPECT_EQ(rotation["entity"]["generation"], 2u);
    EXPECT_FLOAT_EQ(rotation["euler_degrees"][0].get<float>(), 9999.0f);
    EXPECT_FLOAT_EQ(rotation["axis_center"][1][0].get<float>(), 120.0f);
    EXPECT_EQ(rotation["rederive_count"], 7u);
}

TEST(ParseUiMouseQueryTests, MoveNeedsFiniteCoordinatesOtherActionsDoNot)
{
    const ParsedUiMouseQuery move = ParseUiMouseQuery("move", "12.5", "-3", "");
    ASSERT_TRUE(move.valid) << move.errorMessage;
    EXPECT_EQ(move.injection.action, gte::MouseInjectAction::Move);
    EXPECT_FLOAT_EQ(move.injection.x, 12.5f);
    EXPECT_FLOAT_EQ(move.injection.y, -3.0f);
    EXPECT_EQ(move.injection.button, gte::MouseInjectButton::Left);

    EXPECT_FALSE(ParseUiMouseQuery("move", "", "1", "").valid);
    EXPECT_FALSE(ParseUiMouseQuery("move", "nan", "1", "").valid);
    EXPECT_FALSE(ParseUiMouseQuery("move", "1e30", "1", "").valid);

    const ParsedUiMouseQuery down = ParseUiMouseQuery("down", "", "", "right");
    ASSERT_TRUE(down.valid) << down.errorMessage;
    EXPECT_EQ(down.injection.button, gte::MouseInjectButton::Right);
    EXPECT_TRUE(ParseUiMouseQuery("release", "", "", "").valid);
    EXPECT_FALSE(ParseUiMouseQuery("click", "", "", "").valid);
    EXPECT_FALSE(ParseUiMouseQuery("up", "", "", "side").valid);
}

TEST(InspectorResponseTests, SmallAnswersAreValidJson)
{
    const nlohmann::json started = nlohmann::json::parse(BuildInspectorBuildStartedResponseJson());
    EXPECT_TRUE(started["started"].get<bool>());

    const nlohmann::json split = nlohmann::json::parse(BuildSplitViewsResponseJson(true));
    EXPECT_TRUE(split["split"].get<bool>());

    const nlohmann::json selected = nlohmann::json::parse(BuildSelectAssetResponseJson("C:\\P\\a.gta"));
    EXPECT_EQ(selected["selected_path"], "C:\\P\\a.gta");
}

TEST(ParseHierarchyStateQueryTests, DefaultsAndCap)
{
    const ParsedHierarchyStateQuery blank = ParseHierarchyStateQuery("", "");
    ASSERT_TRUE(blank.valid) << blank.errorMessage;
    EXPECT_EQ(blank.first, 0u);
    EXPECT_EQ(blank.count, 200u);

    const ParsedHierarchyStateQuery capped = ParseHierarchyStateQuery("5", "999999");
    ASSERT_TRUE(capped.valid) << capped.errorMessage;
    EXPECT_EQ(capped.first, 5u);
    EXPECT_EQ(capped.count, 2000u);

    EXPECT_FALSE(ParseHierarchyStateQuery("-1", "").valid);
    EXPECT_FALSE(ParseHierarchyStateQuery("", "abc").valid);
}

TEST(HierarchyStateResponseTests, JsonCarriesEveryKey)
{
    gte::HierarchyStateReport report;
    report.visible = true;
    report.entityCount = 3;
    report.rowCount = 2;
    report.collapsedCount = 1;
    report.first = 4;
    report.buildMs = 0.5;
    report.flattenMs = 0.25;
    report.drawnRows = 2;

    gte::HierarchyStateRow root;
    root.entity = gte::Entity{ 7, 2 };
    root.label = "Root";
    root.hasChildren = true;
    root.expanded = false;
    root.selected = true;
    gte::HierarchyStateRow child;
    child.entity = gte::Entity{ 9, 1 };
    child.parent = root.entity;
    child.depth = 1;
    child.siblingPosition = 3;
    child.label = "Child";
    report.rows = { root, child };

    const nlohmann::json body = nlohmann::json::parse(BuildHierarchyStateResponseJson(report));
    for (const char* key : { "visible", "entityCount", "rowCount", "collapsedCount", "first", "rows", "buildMs", "rowsReused",
             "flattenMs", "drawnRows" }) {
        EXPECT_TRUE(body.contains(key)) << key;
    }
    ASSERT_EQ(body["rows"].size(), 2u);
    for (const char* key : { "entity", "parent", "depth", "siblingPosition", "label", "hasChildren", "expanded", "selected" }) {
        EXPECT_TRUE(body["rows"][0].contains(key)) << key;
    }
    EXPECT_EQ(body["rows"][0]["entity"], "7/2");
    EXPECT_TRUE(body["rows"][0]["parent"].is_null());
    EXPECT_EQ(body["rows"][1]["parent"], "7/2");
    EXPECT_EQ(body["rows"][1]["depth"], 1);
    EXPECT_EQ(body["rows"][1]["siblingPosition"], 3);
    EXPECT_FALSE(body["rows"][0]["expanded"].get<bool>());
    EXPECT_TRUE(body["rows"][0]["selected"].get<bool>());
    EXPECT_EQ(body["first"], 4);
    EXPECT_FALSE(body["rowsReused"].get<bool>());
    EXPECT_EQ(body["flattenMs"], 0.25);

    report.rowsReused = true;
    const nlohmann::json reused = nlohmann::json::parse(BuildHierarchyStateResponseJson(report));
    EXPECT_TRUE(reused["rowsReused"].get<bool>());
    EXPECT_TRUE(reused["flattenMs"].is_null());
    EXPECT_EQ(reused["buildMs"], 0.5);
}

TEST(HierarchyStateResponseTests, HiddenPanelHasNoRowsAndNullTimings)
{
    gte::HierarchyStateReport report;
    report.entityCount = 10;

    const nlohmann::json body = nlohmann::json::parse(BuildHierarchyStateResponseJson(report));
    EXPECT_FALSE(body["visible"].get<bool>());
    EXPECT_EQ(body["entityCount"], 10);
    EXPECT_TRUE(body["rows"].empty());
    EXPECT_TRUE(body["buildMs"].is_null());
    EXPECT_TRUE(body["flattenMs"].is_null());
    EXPECT_FALSE(body["rowsReused"].get<bool>());
    EXPECT_TRUE(body["drawnRows"].is_null());
}

TEST(ParsePanelVisibleQueryTests, NameAndVisibleAreValidated)
{
    const ParsedPanelVisibleQuery hide = ParsePanelVisibleQuery("Hierarchy", "false");
    ASSERT_TRUE(hide.valid) << hide.errorMessage;
    EXPECT_EQ(hide.panel, gte::TogglePanel::Hierarchy);
    EXPECT_FALSE(hide.visible);

    const ParsedPanelVisibleQuery show = ParsePanelVisibleQuery("Hierarchy", "true");
    ASSERT_TRUE(show.valid) << show.errorMessage;
    EXPECT_TRUE(show.visible);

    const ParsedPanelVisibleQuery unknown = ParsePanelVisibleQuery("Nope", "true");
    EXPECT_FALSE(unknown.valid);
    EXPECT_NE(unknown.errorMessage.find("Hierarchy"), std::string::npos); // lists the supported names

    EXPECT_FALSE(ParsePanelVisibleQuery("", "true").valid);
    EXPECT_FALSE(ParsePanelVisibleQuery("Hierarchy", "").valid);
    EXPECT_FALSE(ParsePanelVisibleQuery("Hierarchy", "TRUE").valid);
    EXPECT_FALSE(ParsePanelVisibleQuery("hierarchy", "true").valid);
}

TEST(PanelVisibleResponseTests, JsonKeysArePinned)
{
    const nlohmann::json body =
        nlohmann::json::parse(BuildPanelVisibleResponseJson(gte::TogglePanel::Hierarchy, false));
    EXPECT_EQ(body.size(), 3u);
    EXPECT_TRUE(body["success"].get<bool>());
    EXPECT_EQ(body["name"], "Hierarchy");
    EXPECT_FALSE(body["visible"].get<bool>());
}

TEST(ClosePresetSceneResponseTests, JsonKeysArePinned)
{
    const nlohmann::json body = nlohmann::json::parse(BuildClosePresetSceneResponseJson("Closed the Hi-Z demo."));
    EXPECT_EQ(body.size(), 2u);
    EXPECT_TRUE(body["success"].get<bool>());
    EXPECT_EQ(body["message"], "Closed the Hi-Z demo.");
}

TEST(ParseHierarchyActionRequestTests, KindAndEntitiesAreValidated)
{
    const ParsedHierarchyActionRequest reorder = ParseHierarchyActionRequest(
        R"({"kind":"reorder","dragged":{"index":4,"generation":2},"target":"9/1","insertAfter":true})");
    ASSERT_TRUE(reorder.valid) << reorder.errorMessage;
    EXPECT_EQ(reorder.command.kind, gte::HierarchyActionKind::Reorder);
    EXPECT_EQ(reorder.command.dragged, (gte::Entity{ 4, 2 }));
    EXPECT_EQ(reorder.command.target, (gte::Entity{ 9, 1 }));
    EXPECT_TRUE(reorder.command.insertAfter);

    const ParsedHierarchyActionRequest reparent =
        ParseHierarchyActionRequest(R"({"kind":"reparent","dragged":"4/2","target":{"index":9,"generation":1}})");
    ASSERT_TRUE(reparent.valid) << reparent.errorMessage;
    EXPECT_EQ(reparent.command.kind, gte::HierarchyActionKind::Reparent);
    EXPECT_FALSE(reparent.command.insertAfter);

    const ParsedHierarchyActionRequest toggle = ParseHierarchyActionRequest(R"({"kind":"toggle","target":"9/1"})");
    ASSERT_TRUE(toggle.valid) << toggle.errorMessage;
    EXPECT_EQ(toggle.command.kind, gte::HierarchyActionKind::Toggle);
}

TEST(ParseHierarchyActionRequestTests, BadBodiesAreRefused)
{
    EXPECT_FALSE(ParseHierarchyActionRequest("").valid);
    EXPECT_FALSE(ParseHierarchyActionRequest("[]").valid);
    EXPECT_FALSE(ParseHierarchyActionRequest(R"({"target":"9/1"})").valid);                                // no kind
    EXPECT_FALSE(ParseHierarchyActionRequest(R"({"kind":"move","dragged":"4/2","target":"9/1"})").valid);   // bad kind
    EXPECT_FALSE(ParseHierarchyActionRequest(R"({"kind":"reorder","target":"9/1"})").valid);                // no dragged
    EXPECT_FALSE(ParseHierarchyActionRequest(R"({"kind":"reparent","dragged":"4/2"})").valid);              // no target
    EXPECT_FALSE(ParseHierarchyActionRequest(R"({"kind":"toggle","target":"9/0"})").valid);                 // generation 0
    EXPECT_FALSE(ParseHierarchyActionRequest(R"({"kind":"toggle","target":"nine"})").valid);
    EXPECT_FALSE(ParseHierarchyActionRequest(R"({"kind":"toggle","target":{"index":1}})").valid);
    EXPECT_FALSE(ParseHierarchyActionRequest(R"({"kind":"toggle","target":"9/1","insertAfter":"yes"})").valid);
}

TEST(HierarchyActionResponseTests, JsonKeysArePinned)
{
    gte::HierarchyActionReport report;
    report.success = true;
    report.kind = "reorder";
    report.changed = true;
    report.firstRow = 3;
    gte::HierarchyStateRow row;
    row.entity = gte::Entity{ 7, 2 };
    row.label = "Root";
    report.rows = { row };

    const nlohmann::json body = nlohmann::json::parse(BuildHierarchyActionResponseJson(report));
    EXPECT_EQ(body.size(), 5u);
    for (const char* key : { "success", "kind", "changed", "firstRow", "rows" }) {
        EXPECT_TRUE(body.contains(key)) << key;
    }
    EXPECT_EQ(body["kind"], "reorder");
    EXPECT_EQ(body["firstRow"], 3);
    ASSERT_EQ(body["rows"].size(), 1u);
    EXPECT_EQ(body["rows"][0].size(), 8u);
    EXPECT_EQ(body["rows"][0]["entity"], "7/2");
}

} // namespace
