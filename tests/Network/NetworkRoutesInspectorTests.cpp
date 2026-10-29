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
    for (const char* key : { "visible", "entityCount", "rowCount", "collapsedCount", "first", "rows", "buildMs", "flattenMs",
             "drawnRows" }) {
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
    EXPECT_TRUE(body["drawnRows"].is_null());
}

} // namespace
