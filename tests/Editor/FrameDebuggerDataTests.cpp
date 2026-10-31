// Tier-1 tests for the Frame Debugger data model (src/Editor/FrameDebuggerData.h). No ImGui or GPU device involved.

#include "Editor/FrameDebuggerData.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace gte {
namespace {

TEST(FrameDebuggerDataTest, FormatFrameStepperLabelTest)
{
    EXPECT_EQ(FormatFrameStepperLabel(0, 0), "0 of 0");
    EXPECT_EQ(FormatFrameStepperLabel(-1, 0), "0 of 0");
    EXPECT_EQ(FormatFrameStepperLabel(0, 2117), "1 of 2117");
    EXPECT_EQ(FormatFrameStepperLabel(2116, 2117), "2117 of 2117");
}

TEST(FrameDebuggerDataTest, ClampSelectedEventIndexTest)
{
    EXPECT_EQ(ClampSelectedEventIndex(-1, 0), -1);
    EXPECT_EQ(ClampSelectedEventIndex(5, 0), -1);
    EXPECT_EQ(ClampSelectedEventIndex(-1, 10), 0);
    EXPECT_EQ(ClampSelectedEventIndex(999, 10), 9);
    EXPECT_EQ(ClampSelectedEventIndex(4, 10), 4); // Identity for an already-valid index.
}

TEST(FrameDebuggerDataTest, ComputeAspectFitImageRectExactAspectMatchFillsWithNoOffset)
{
    // Same 2:1 aspect: fills the box with no offset.
    const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(800.0f, 400.0f, 1600.0f, 800.0f);
    EXPECT_FLOAT_EQ(fit.width, 800.0f);
    EXPECT_FLOAT_EQ(fit.height, 400.0f);
    EXPECT_FLOAT_EQ(fit.offsetX, 0.0f);
    EXPECT_FLOAT_EQ(fit.offsetY, 0.0f);
}

TEST(FrameDebuggerDataTest, ComputeAspectFitImageRectWiderSourceLetterboxesTopAndBottom)
{
    // Wide source: width fills, height is smaller and centered vertically.
    const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(400.0f, 400.0f, 800.0f, 200.0f);
    EXPECT_FLOAT_EQ(fit.width, 400.0f);
    EXPECT_LT(fit.height, 400.0f);
    EXPECT_FLOAT_EQ(fit.offsetX, 0.0f);
    EXPECT_GT(fit.offsetY, 0.0f);
}

TEST(FrameDebuggerDataTest, ComputeAspectFitImageRectTallerSourcePillarboxesLeftAndRight)
{
    // Tall source: height fills, width is smaller and centered horizontally.
    const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(400.0f, 400.0f, 200.0f, 800.0f);
    EXPECT_FLOAT_EQ(fit.height, 400.0f);
    EXPECT_LT(fit.width, 400.0f);
    EXPECT_FLOAT_EQ(fit.offsetY, 0.0f);
    EXPECT_GT(fit.offsetX, 0.0f);
}

TEST(FrameDebuggerDataTest, ComputeAspectFitImageRectDegenerateInputFillsAtOrigin)
{
    // Any non-positive input fills the box at (0, 0), never NaN or negative.
    {
        const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(0.0f, 400.0f, 100.0f, 100.0f);
        EXPECT_FLOAT_EQ(fit.offsetX, 0.0f);
        EXPECT_FLOAT_EQ(fit.offsetY, 0.0f);
        EXPECT_FLOAT_EQ(fit.width, 0.0f);
        EXPECT_FLOAT_EQ(fit.height, 400.0f);
    }
    {
        const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(400.0f, 400.0f, -1.0f, 100.0f);
        EXPECT_FLOAT_EQ(fit.offsetX, 0.0f);
        EXPECT_FLOAT_EQ(fit.offsetY, 0.0f);
        EXPECT_FLOAT_EQ(fit.width, 400.0f);
        EXPECT_FLOAT_EQ(fit.height, 400.0f);
    }
}

TEST(FrameDebuggerDataTest, ComputeAspectFitImageRectRealWorldCase417x333In800x400Box)
{
    // A 417x333 source in an 800x400 box is taller than the box: height-constrained.
    const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(800.0f, 400.0f, 417.0f, 333.0f);
    const float expectedWidth = 400.0f * (417.0f / 333.0f); // ~500.9009
    EXPECT_FLOAT_EQ(fit.height, 400.0f);
    EXPECT_NEAR(fit.width, expectedWidth, 0.01f);
    EXPECT_FLOAT_EQ(fit.offsetY, 0.0f);
    EXPECT_NEAR(fit.offsetX, (800.0f - expectedWidth) * 0.5f, 0.01f);
}

TEST(FrameDebuggerDataTest, FindEventDetailsByIndexTest)
{
    // A hand-built nested tree proves the recursive lookup.
    FrameDebuggerEventDetails leafDetails;
    leafDetails.eventIndex = 3;
    leafDetails.shaderName = "Standard/DistinguishableTestShader";

    FrameDebuggerEventNode leaf;
    leaf.name = "Draw Mesh Foo";
    leaf.isDrawCall = true;
    leaf.eventIndex = 3;
    leaf.details = leafDetails;

    FrameDebuggerEventNode innerGroup;
    innerGroup.name = "Render.OpaqueGeometry";
    innerGroup.isDrawCall = false;
    innerGroup.children.push_back(leaf);

    FrameDebuggerEventNode outerGroup;
    outerGroup.name = "Drawing";
    outerGroup.isDrawCall = false;
    outerGroup.children.push_back(innerGroup);

    FrameDebuggerSnapshot snapshot;
    snapshot.rootNodes.push_back(outerGroup);
    snapshot.totalEventCount = 1;

    const std::optional<FrameDebuggerEventDetails> found = FindEventDetailsByIndex(snapshot, 3);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->shaderName, "Standard/DistinguishableTestShader");

    EXPECT_FALSE(FindEventDetailsByIndex(snapshot, 999).has_value());
    EXPECT_FALSE(FindEventDetailsByIndex(snapshot, -1).has_value());
}

TEST(FrameDebuggerDataTest, FindEventDetailsByIndexFindsAPerEntityChildLeafOfASelectableParentLeaf)
{
    FrameDebuggerEventDetails childDetails;
    childDetails.eventIndex = 5;
    childDetails.passName = "RenderOpaque (Entity Draw)";
    childDetails.shaderName = "Mesh.vert/Mesh.frag (PositionNormal)";

    FrameDebuggerEventNode childLeaf;
    childLeaf.name = "terrain (Entity 2)";
    childLeaf.isDrawCall = true;
    childLeaf.eventIndex = 5;
    childLeaf.details = childDetails;

    // The parent is itself a selectable draw call and also has children.
    FrameDebuggerEventDetails parentDetails;
    parentDetails.eventIndex = 4;
    parentDetails.passName = "RenderOpaque";

    FrameDebuggerEventNode parentLeaf;
    parentLeaf.name = "RenderOpaque";
    parentLeaf.isDrawCall = true;
    parentLeaf.eventIndex = 4;
    parentLeaf.details = parentDetails;
    parentLeaf.children.push_back(childLeaf);

    FrameDebuggerSnapshot snapshot;
    snapshot.rootNodes.push_back(parentLeaf);
    snapshot.totalEventCount = 2;

    const std::optional<FrameDebuggerEventDetails> foundParent = FindEventDetailsByIndex(snapshot, 4);
    ASSERT_TRUE(foundParent.has_value());
    EXPECT_EQ(foundParent->passName, "RenderOpaque");

    const std::optional<FrameDebuggerEventDetails> foundChild = FindEventDetailsByIndex(snapshot, 5);
    ASSERT_TRUE(foundChild.has_value());
    EXPECT_EQ(foundChild->passName, "RenderOpaque (Entity Draw)");
    EXPECT_EQ(foundChild->shaderName, "Mesh.vert/Mesh.frag (PositionNormal)");
}

TEST(FrameDebuggerDataTest, FormatVectorPropertyTest)
{
    FrameDebuggerVectorProperty color;
    color.name = "_Color";
    color.x = 1.0f;
    color.y = 1.0f;
    color.z = 1.0f;
    color.w = 1.0f;
    EXPECT_EQ(FormatVectorProperty(color), "(1, 1, 1, 1)");

    FrameDebuggerVectorProperty other;
    other.name = "_Other";
    other.x = 0.5f;
    other.y = 0.0f;
    other.z = 0.0f;
    other.w = 0.0f;
    EXPECT_EQ(FormatVectorProperty(other), "(0.5, 0, 0, 0)");
}

TEST(FrameDebuggerDataTest, FormatMatrixPropertyTest)
{
    FrameDebuggerMatrixProperty matrix;
    matrix.name = "unity_MatrixVP";
    matrix.values = { 0.001f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0019f, 0.0f, 0.0f, 0.0f, 0.0f, 0.00023f, 0.5f, 0.0f, 0.0f,
        0.0f, 1.0f };

    const std::string formatted = FormatMatrixProperty(matrix);
    EXPECT_EQ(formatted.substr(0, formatted.find('\n')), "0.001 0 0 0");
    EXPECT_EQ(formatted.substr(formatted.rfind('\n') + 1), "0 0 0 1");
    EXPECT_EQ(std::count(formatted.begin(), formatted.end(), '\n'), 3);
}

// Covers every FrameDebuggerStepPreviewKind.
TEST(FrameDebuggerDataTest, ChooseFrameDebuggerPreviewSourceTest)
{
    // No captured frame: None, whatever the step kind.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(
                  false, FrameDebuggerStepPreviewKind::PostComposite, false, false, false),
        FrameDebuggerPreviewSourceChoice::None);

    // NotYetDrawn always wins, even when previews exist.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::NotYetDrawn, true, true, true),
        FrameDebuggerPreviewSourceChoice::NotYetDrawn);
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::NotYetDrawn, false, false, false),
        FrameDebuggerPreviewSourceChoice::NotYetDrawn);

    // PreComposite shows the plain preview even when a composited one exists.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PreComposite, true, true, false),
        FrameDebuggerPreviewSourceChoice::Preview);
    // PreComposite without a plain preview: None.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PreComposite, false, true, false),
        FrameDebuggerPreviewSourceChoice::None);

    // PostComposite prefers the composited preview.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PostComposite, true, true, false),
        FrameDebuggerPreviewSourceChoice::CompositedPreview);
    // PostComposite falls back to the plain preview.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PostComposite, true, false, false),
        FrameDebuggerPreviewSourceChoice::Preview);
    // PostComposite with neither: None.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PostComposite, false, false, false),
        FrameDebuggerPreviewSourceChoice::None);

    // PerObjectStep shows its own retained image when the index resolves.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PerObjectStep, true, true, true),
        FrameDebuggerPreviewSourceChoice::PerObjectStepPreview);
    // PerObjectStep never falls back to a whole-frame image.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PerObjectStep, true, true, false),
        FrameDebuggerPreviewSourceChoice::None);
}

TEST(FrameDebuggerDataTest, EntityLeafShapeIsCorrect)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot renderOpaque;
    renderOpaque.name = "RenderOpaque";
    graphSnapshot.passesInExecutionOrder.push_back(renderOpaque);

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", {}, 1045458);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 1u);

    const FrameDebuggerEventNode& terrainLeaf = renderOpaqueLeaf.children[0];
    EXPECT_EQ(terrainLeaf.name, "terrain (Entity 2)");
    ASSERT_TRUE(terrainLeaf.details.has_value());
    EXPECT_EQ(terrainLeaf.details->passName, "RenderOpaque (Entity Draw)");
    EXPECT_EQ(terrainLeaf.details->eventLabel, "Draw Mesh");
    EXPECT_EQ(terrainLeaf.details->zTest, "Less");
    EXPECT_EQ(terrainLeaf.details->zWrite, "On");

    bool foundEntityIdentity = false;
    for (const FrameDebuggerVectorProperty& vec : terrainLeaf.details->vectors) {
        if (vec.name == "Entity (Index, Generation)") {
            foundEntityIdentity = true;
            EXPECT_FLOAT_EQ(vec.x, 2.0f);
            EXPECT_FLOAT_EQ(vec.y, 0.0f);
        }
    }
    EXPECT_TRUE(foundEntityIdentity);
}

} // namespace
} // namespace gte
