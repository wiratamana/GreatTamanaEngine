// Unit tests for ComputeGraphLayout() (src/Editor/RenderGraphLayout.h/.cpp).
// Pure function of a hand-fabricated rg::RenderGraphRegimeMetadata - no
// ImGui, no Vulkan, no live RenderGraph anywhere in this file.

#include "Editor/RenderGraphLayout.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

rg::RenderGraphPassMetadata MakePass(const char* name, std::uint32_t order, bool isCulled = false)
{
    rg::RenderGraphPassMetadata pass;
    pass.name = name;
    pass.renderPassEventOrder = order;
    pass.isCulled = isCulled;
    return pass;
}

std::uint32_t ColumnOf(const GraphLayout& layout, const std::string& passName)
{
    for (const GraphNodeLayout& node : layout.nodes) {
        if (node.passName == passName) {
            return node.column;
        }
    }
    ADD_FAILURE() << "Pass not found in layout: " << passName;
    return 0;
}

TEST(RenderGraphLayoutTest, DistinctOrdinalsGetDistinctAscendingColumns)
{
    rg::RenderGraphRegimeMetadata regime;
    regime.passes.push_back(MakePass("Early", 10));
    regime.passes.push_back(MakePass("Middle", 20));
    regime.passes.push_back(MakePass("Late", 30));

    const GraphLayout layout = ComputeGraphLayout(regime);

    EXPECT_LT(ColumnOf(layout, "Early"), ColumnOf(layout, "Middle"));
    EXPECT_LT(ColumnOf(layout, "Middle"), ColumnOf(layout, "Late"));
}

TEST(RenderGraphLayoutTest, SameOrdinalSharesColumnAtDistinctRows)
{
    rg::RenderGraphRegimeMetadata regime;
    regime.passes.push_back(MakePass("GameView", 10));
    regime.passes.push_back(MakePass("SceneView", 10));

    const GraphLayout layout = ComputeGraphLayout(regime);

    ASSERT_EQ(layout.nodes.size(), 2u);
    EXPECT_EQ(layout.nodes[0].column, layout.nodes[1].column);
    EXPECT_NE(layout.nodes[0].row, layout.nodes[1].row);
}

// The exact scenario a first-seen-position layout would get wrong:
// RenderGraphRegimeMetadata::passes lists survivors first, then every
// culled pass appended at the tail regardless of its own real tier (see
// RenderGraphSnapshot.h). A mid-tier pass culled this frame must still
// land strictly between an earlier-tier and a later-tier survivor.
TEST(RenderGraphLayoutTest, CulledMidTierPassStillLandsBetweenSurvivingNeighbors)
{
    rg::RenderGraphRegimeMetadata regime;
    regime.passes.push_back(MakePass("EarlySurvivor", 10));  // Survivor, first in the vector.
    regime.passes.push_back(MakePass("LateSurvivor", 30));   // Survivor, second in the vector.
    regime.passes.push_back(MakePass("MidCulled", 20, /*isCulled=*/true)); // Culled, appended last.

    const GraphLayout layout = ComputeGraphLayout(regime);

    const std::uint32_t earlyColumn = ColumnOf(layout, "EarlySurvivor");
    const std::uint32_t midColumn = ColumnOf(layout, "MidCulled");
    const std::uint32_t lateColumn = ColumnOf(layout, "LateSurvivor");

    EXPECT_LT(earlyColumn, midColumn);
    EXPECT_LT(midColumn, lateColumn);
}

TEST(RenderGraphLayoutTest, DifferingFirstAndLastUsePassProducesOneEdge)
{
    rg::RenderGraphRegimeMetadata regime;
    regime.passes.push_back(MakePass("WritePass", 10));
    regime.passes.push_back(MakePass("ReadPass", 20));

    rg::RenderGraphResourceMetadata resource;
    resource.name = "Shared";
    resource.firstUsePassName = "WritePass";
    resource.lastUsePassName = "ReadPass";
    regime.resources.push_back(resource);

    const GraphLayout layout = ComputeGraphLayout(regime);

    ASSERT_EQ(layout.edges.size(), 1u);
    EXPECT_EQ(layout.edges[0].fromPass, "WritePass");
    EXPECT_EQ(layout.edges[0].toPass, "ReadPass");
    EXPECT_EQ(layout.edges[0].resourceName, "Shared");
}

TEST(RenderGraphLayoutTest, SameFirstAndLastUsePassProducesNoEdge)
{
    rg::RenderGraphRegimeMetadata regime;
    regime.passes.push_back(MakePass("SelfContainedPass", 10));

    rg::RenderGraphResourceMetadata resource;
    resource.name = "Local";
    resource.firstUsePassName = "SelfContainedPass";
    resource.lastUsePassName = "SelfContainedPass";
    regime.resources.push_back(resource);

    const GraphLayout layout = ComputeGraphLayout(regime);

    EXPECT_TRUE(layout.edges.empty());
}

} // namespace
} // namespace gte
