// Tier 1 tests for the cluster viewer and clone routes: pure parsing and JSON building only.

#include "Network/NetworkRoutes.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

using namespace gte;
using namespace gte::Network;

TEST(ClusterViewerSetQueryTests, PairsAreKeptInOrder)
{
    const ParsedClusterViewerSetQuery parsed =
        ParseClusterViewerSetQuery({ { "drawMode", "cull" }, { "frustum", "true" } });
    ASSERT_TRUE(parsed.valid) << parsed.errorMessage;
    ASSERT_EQ(parsed.pairs.size(), 2u);
    EXPECT_EQ(parsed.pairs[0].key, "drawMode");
    EXPECT_EQ(parsed.pairs[1].value, "true");
}

TEST(ClusterViewerSetQueryTests, EmptyDuplicateAndEmptyKeyAreRefused)
{
    EXPECT_FALSE(ParseClusterViewerSetQuery({}).valid);
    EXPECT_FALSE(ParseClusterViewerSetQuery({ { "cone", "true" }, { "cone", "false" } }).valid);
    EXPECT_FALSE(ParseClusterViewerSetQuery({ { "", "true" } }).valid);
}

TEST(PickQueryTests, FractionsInsideZeroToOneAreAccepted)
{
    const ParsedPickQuery parsed = ParsePickQuery("0.25", "1");
    ASSERT_TRUE(parsed.valid) << parsed.errorMessage;
    EXPECT_FLOAT_EQ(parsed.x, 0.25f);
    EXPECT_FLOAT_EQ(parsed.y, 1.0f);
}

TEST(PickQueryTests, MissingOutOfRangeAndNonFiniteAreRefused)
{
    EXPECT_FALSE(ParsePickQuery("", "0.5").valid);
    EXPECT_FALSE(ParsePickQuery("0.5", "").valid);
    EXPECT_FALSE(ParsePickQuery("-0.1", "0.5").valid);
    EXPECT_FALSE(ParsePickQuery("0.5", "1.01").valid);
    EXPECT_FALSE(ParsePickQuery("nan", "0.5").valid);
    EXPECT_FALSE(ParsePickQuery("0.5x", "0.5").valid);
}

TEST(ClusterViewerImageQueryTests, TargetAndOverlay)
{
    const ParsedClusterViewerImageQuery defaults = ParseClusterViewerImageQuery("", "");
    ASSERT_TRUE(defaults.valid);
    EXPECT_FALSE(defaults.debugCamera);
    EXPECT_FALSE(defaults.overlay);

    const ParsedClusterViewerImageQuery debug = ParseClusterViewerImageQuery("debug", "true");
    ASSERT_TRUE(debug.valid);
    EXPECT_TRUE(debug.debugCamera);
    EXPECT_TRUE(debug.overlay);

    EXPECT_FALSE(ParseClusterViewerImageQuery("other", "").valid);
    EXPECT_FALSE(ParseClusterViewerImageQuery("view", "yes").valid);
}

TEST(ClusterViewerStatusTests, ErrorsMapToHttpStatus)
{
    EXPECT_EQ(HttpStatusForError(ClusterViewerError::BadInput), 400);
    EXPECT_EQ(HttpStatusForError(ClusterViewerError::NotOpen), 409);
    EXPECT_EQ(HttpStatusForError(ClusterViewerError::NoValidMesh), 409);
    EXPECT_EQ(HttpStatusForError(ClusterViewerError::Unsupported), 409);
    EXPECT_EQ(HttpStatusForError(ClusterViewerError::Unavailable), 503);
}

TEST(ClusterViewerStateJsonTests, CarriesTheKeyFields)
{
    ClusterViewerReport report;
    report.open = true;
    report.valid = true;
    report.path = "C:\\Project\\rock_clustered.gta";
    report.mesh.clusters = 10905;
    report.mesh.clusterModes = true;
    report.settings = { { "drawMode", "cull" } };
    report.cull.ok = true;
    report.cull.total = 100;
    report.cull.visible = 40;
    report.cull.byFrustum = 60;
    report.cull.nested = true;
    report.truth.falseCulls = 0;
    report.clusterPathSupported = true;

    const nlohmann::json json = nlohmann::json::parse(BuildClusterViewerStateJson(report));
    EXPECT_TRUE(json["open"].get<bool>());
    EXPECT_EQ(json["mesh"]["clusters"].get<int>(), 10905);
    EXPECT_EQ(json["settings"]["drawMode"].get<std::string>(), "cull");
    EXPECT_EQ(json["cull"]["total"].get<int>(), 100);
    EXPECT_EQ(json["cull"]["byFrustum"].get<int>(), 60);
    EXPECT_TRUE(json["cull"]["nested"].get<bool>());
    EXPECT_EQ(json["truth"]["falseCulls"].get<int>(), 0);
    EXPECT_TRUE(json["clusterPathSupported"].get<bool>());
}

TEST(ClusterViewerCommandResponseTests, FailureUsesTheGenericErrorShape)
{
    const ClusterViewerResult failed{ false, ClusterViewerError::NotOpen, "viewer is not open" };
    const nlohmann::json json = nlohmann::json::parse(BuildClusterViewerCommandResponseJson(failed, {}));
    EXPECT_FALSE(json["success"].get<bool>());
    EXPECT_EQ(json["error"].get<std::string>(), "viewer is not open");

    const ClusterViewerResult ok{ true, ClusterViewerError::None, "done" };
    const nlohmann::json good = nlohmann::json::parse(BuildClusterViewerCommandResponseJson(ok, {}));
    EXPECT_TRUE(good["success"].get<bool>());
    EXPECT_TRUE(good.contains("state"));
}

TEST(CloneClustersRequestTests, OnlyGtaPathIsRequired)
{
    const ParsedCloneClustersRequest parsed = ParseCloneClustersRequest(R"({"gta_path":"C:\\P\\rock.gta"})");
    ASSERT_TRUE(parsed.valid) << parsed.errorMessage;
    EXPECT_EQ(parsed.weld, "auto");
    EXPECT_FALSE(parsed.openViewer);
    EXPECT_EQ(parsed.maxVertices, 64u);
    EXPECT_EQ(parsed.maxTriangles, 124u);
}

TEST(CloneClustersRequestTests, WeldOptionsAndLimits)
{
    const ParsedCloneClustersRequest parsed = ParseCloneClustersRequest(
        R"({"gta_path":"C:\\P\\a.gta","weld":"position_only","open_viewer":true,"max_vertices":32,"max_triangles":64,"cone_weight":0.25})");
    ASSERT_TRUE(parsed.valid) << parsed.errorMessage;
    EXPECT_EQ(parsed.weld, "position_only");
    EXPECT_TRUE(parsed.openViewer);
    EXPECT_EQ(parsed.maxVertices, 32u);
    EXPECT_EQ(parsed.maxTriangles, 64u);
    EXPECT_FLOAT_EQ(parsed.coneWeight, 0.25f);
}

TEST(CloneClustersRequestTests, BadInputIsRefused)
{
    EXPECT_FALSE(ParseCloneClustersRequest("not json").valid);
    EXPECT_FALSE(ParseCloneClustersRequest("[]").valid);
    EXPECT_FALSE(ParseCloneClustersRequest("{}").valid);
    EXPECT_FALSE(ParseCloneClustersRequest(R"({"gta_path":"a.gta","weld":"fancy"})").valid);
    EXPECT_FALSE(ParseCloneClustersRequest(R"({"gta_path":"a.gta","open_viewer":"yes"})").valid);
    EXPECT_FALSE(ParseCloneClustersRequest(R"({"gta_path":"a.gta","max_triangles":10})").valid);
}

TEST(CloneClustersResponseTests, JsonHoldsTheCreatedFile)
{
    CloneClustersResponseView view;
    view.createdPath = "C:\\P\\a_clustered.gta";
    view.guid = "guid-1";
    view.weldUsed = "position_only";
    view.weldFellBack = true;
    view.clusterCount = 10905;
    const nlohmann::json json = nlohmann::json::parse(BuildCloneClustersResponseJson(view));
    EXPECT_TRUE(json["success"].get<bool>());
    EXPECT_EQ(json["created_path"].get<std::string>(), "C:\\P\\a_clustered.gta");
    EXPECT_EQ(json["weld_used"].get<std::string>(), "position_only");
    EXPECT_TRUE(json["weld_fell_back"].get<bool>());
    EXPECT_EQ(json["cluster_count"].get<int>(), 10905);
}

} // namespace
