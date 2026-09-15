// Unit tests for the Frame Debugger's real snapshot builder
// (src/Editor/FrameDebuggerData.h's BuildRealFrameDebuggerSnapshot()) - pure,
// ImGui-free, no-live-VkDevice-needed reshape of a hand-fabricated
// gte::rg::RenderGraphSnapshot + FrameDebuggerCaptureContext, mirroring
// tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp's own "hand-built
// snapshot in, real result out" precedent. Only built when GTE_ENABLE_EDITOR
// is ON, since FrameDebuggerData.h/.cpp are only compiled into gte_core then
// (see the root CMakeLists.txt's "Editor Module Structure") - the same
// "zero-touch when off" rule already applied to
// tests/Editor/FrameDebuggerDataTests.cpp/FrameDebuggerCaptureTests.cpp.
//
// task_manager/frame-debugger-3 campaign, PHASE2
// (PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md).

#include "Editor/FrameDebuggerData.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

rg::RenderGraphPassSnapshot MakePass(const std::string& name)
{
    rg::RenderGraphPassSnapshot pass;
    pass.name = name;
    return pass;
}

TEST(FrameDebuggerSnapshotBuilderTest, NoGameViewPassProducesEmptyResult)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("SceneView"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("Present"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, {});

    EXPECT_TRUE(snapshot.rootNodes.empty());
    EXPECT_EQ(snapshot.totalEventCount, 0);
}

TEST(FrameDebuggerSnapshotBuilderTest, GameViewWithNoGpuSkinningProducesExactlyOneLeaf)
{
    rg::RenderGraphSnapshot graphSnapshot;
    // SceneView/Present are real passes in the SAME underlying snapshot -
    // Locked Design Decision #7 (Game-View-only scope) must exclude them
    // even though they're right here alongside "GameView".
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("SceneView"));
    rg::RenderGraphPassSnapshot gameView = MakePass("GameView");
    gameView.stats.drawStats.drawCallCount = 7;
    gameView.stats.drawStats.triangleCount = 250;
    graphSnapshot.passesInExecutionOrder.push_back(gameView);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("Present"));

    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("Mesh.vert/Mesh.frag (PositionNormal)", "MaterialTexture abc123", Mat4::Identity());

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, {});

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    EXPECT_FALSE(root.isDrawCall);
    ASSERT_EQ(root.children.size(), 1u); // No "GPU Skinning" group at all.
    EXPECT_EQ(snapshot.totalEventCount, 1);

    const FrameDebuggerEventNode& leaf = root.children[0];
    EXPECT_TRUE(leaf.isDrawCall);
    EXPECT_EQ(leaf.eventIndex, 0);
    ASSERT_TRUE(leaf.details.has_value());
    EXPECT_EQ(leaf.details->passName, "GameView");
    EXPECT_EQ(leaf.details->eventLabel, "Draw Mesh");
    EXPECT_EQ(leaf.details->shaderName, "Mesh.vert/Mesh.frag (PositionNormal)");
    ASSERT_EQ(leaf.details->textures.size(), 1u);
    EXPECT_EQ(leaf.details->textures[0].valueLabel, "MaterialTexture abc123");

    // vectors: clear color + real aggregate DrawStats from the graph
    // snapshot's own "GameView" entry (NOT from capture.DrawCallCount()).
    ASSERT_EQ(leaf.details->vectors.size(), 2u);
    bool foundDrawStats = false;
    for (const FrameDebuggerVectorProperty& vec : leaf.details->vectors) {
        if (vec.name == "Draw Stats (Calls, Tris)") {
            foundDrawStats = true;
            EXPECT_FLOAT_EQ(vec.x, 7.0f);
            EXPECT_FLOAT_EQ(vec.y, 250.0f);
        }
    }
    EXPECT_TRUE(foundDrawStats);

    // Real, constant Pipeline state (DescribeStandardPipelineState()).
    EXPECT_EQ(leaf.details->blendMode, "Opaque (no blend)");
    EXPECT_EQ(leaf.details->zTest, "Less");
}

TEST(FrameDebuggerSnapshotBuilderTest, TwoGpuSkinningPassesProduceGroupPlusGameViewLeaf)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("SkinPass_A"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("SkinPass_B"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    const std::vector<std::string> gpuSkinningNames{ "SkinPass_A", "SkinPass_B" };
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, gpuSkinningNames);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "GPU Skinning" group + "GameView" leaf, siblings.
    EXPECT_EQ(snapshot.totalEventCount, 3);

    const FrameDebuggerEventNode& gpuGroup = root.children[0];
    EXPECT_FALSE(gpuGroup.isDrawCall);
    EXPECT_EQ(gpuGroup.name, "GPU Skinning");
    ASSERT_EQ(gpuGroup.children.size(), 2u);
    EXPECT_EQ(gpuGroup.children[0].name, "SkinPass_A");
    EXPECT_EQ(gpuGroup.children[0].eventIndex, 0);
    EXPECT_EQ(gpuGroup.children[1].name, "SkinPass_B");
    EXPECT_EQ(gpuGroup.children[1].eventIndex, 1);
    ASSERT_TRUE(gpuGroup.children[0].details.has_value());
    EXPECT_EQ(gpuGroup.children[0].details->eventLabel, "Compute Dispatch");
    EXPECT_EQ(gpuGroup.children[0].details->passName, "GPU Skinning");
    EXPECT_EQ(gpuGroup.children[0].details->blendMode, "n/a (compute pass)");
    EXPECT_TRUE(gpuGroup.children[0].details->textures.empty());
    EXPECT_TRUE(gpuGroup.children[0].details->matrices.empty());

    const FrameDebuggerEventNode& gameViewLeaf = root.children[1];
    EXPECT_TRUE(gameViewLeaf.isDrawCall);
    EXPECT_EQ(gameViewLeaf.eventIndex, 2); // Sequential across the WHOLE tree.
}

TEST(FrameDebuggerSnapshotBuilderTest, GpuSkinningNameWithNoMatchingRealPassAddsNoGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    // Caller-supplied name does not correspond to any real pass THIS frame -
    // must never produce an empty, misleading "GPU Skinning" group.
    const std::vector<std::string> gpuSkinningNames{ "SomeStaleNameFromAPreviousFrame" };
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, gpuSkinningNames);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    ASSERT_EQ(snapshot.rootNodes[0].children.size(), 1u);
    EXPECT_EQ(snapshot.totalEventCount, 1);
}

TEST(FrameDebuggerSnapshotBuilderTest, DistinctPipelineAndTextureNamesProduceDistinctEntriesNotDuplicates)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("PipelineA", "TextureA", Mat4::Identity());
    capture.RecordDraw("PipelineB", "TextureB", Mat4::Identity());
    capture.RecordDraw("PipelineA", "TextureA", Mat4::Identity()); // Repeat - must not duplicate.

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, {});

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    ASSERT_EQ(snapshot.rootNodes[0].children.size(), 1u);
    const FrameDebuggerEventDetails& details = *snapshot.rootNodes[0].children[0].details;
    EXPECT_EQ(details.shaderName, "PipelineA, PipelineB");
    ASSERT_EQ(details.textures.size(), 2u);
    EXPECT_EQ(details.textures[0].valueLabel, "TextureA");
    EXPECT_EQ(details.textures[1].valueLabel, "TextureB");
}

TEST(FrameDebuggerSnapshotBuilderTest, ViewProjectionMatrixRoundTripsWithoutTransposing)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    // A hand-picked, deliberately NON-symmetric matrix: column c holds
    // values {4c+1, 4c+2, 4c+3, 4c+4} - so matrix(row, col) is always
    // DISTINCT from matrix(col, row) whenever row != col, guaranteeing a
    // row/column-major mixup produces visibly wrong values, not a
    // by-coincidence-correct symmetric result.
    const Mat4 matrix(Vec4(1.0f, 2.0f, 3.0f, 4.0f), Vec4(5.0f, 6.0f, 7.0f, 8.0f), Vec4(9.0f, 10.0f, 11.0f, 12.0f),
        Vec4(13.0f, 14.0f, 15.0f, 16.0f));

    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("Pipeline", "", matrix);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, {});

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventDetails& details = *snapshot.rootNodes[0].children[0].details;
    ASSERT_EQ(details.matrices.size(), 1u);
    EXPECT_EQ(details.matrices[0].name, "ViewProjection");
    const std::array<float, 16>& values = details.matrices[0].values;

    // Row-major expectation: values[row*4 + col] == matrix(row, col).
    // matrix(row, col) == columns[col][row], so row 0 reads {1, 5, 9, 13}
    // (the FIRST element of every column), not {1, 2, 3, 4} (column 0's own
    // elements - what a mistaken raw Data() memcpy would produce instead).
    EXPECT_FLOAT_EQ(values[0], 1.0f);
    EXPECT_FLOAT_EQ(values[1], 5.0f);
    EXPECT_FLOAT_EQ(values[2], 9.0f);
    EXPECT_FLOAT_EQ(values[3], 13.0f);
    EXPECT_FLOAT_EQ(values[4], 2.0f);
    EXPECT_FLOAT_EQ(values[5], 6.0f);
    EXPECT_FLOAT_EQ(values[15], 16.0f);
}

TEST(FrameDebuggerSnapshotBuilderTest, RenderTargetInfoIsRealAndNamedGameView)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    FrameDebuggerRenderTargetInfo info;
    info.name = "ThisShouldBeOverwritten";
    info.width = 1920;
    info.height = 1080;
    info.format = "Texture (RGBA8 UNORM)";

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, {}, info);

    EXPECT_EQ(snapshot.renderTarget.name, "GameView");
    EXPECT_EQ(snapshot.renderTarget.width, 1920);
    EXPECT_EQ(snapshot.renderTarget.height, 1080);
    EXPECT_EQ(snapshot.renderTarget.format, "Texture (RGBA8 UNORM)");
}

} // namespace
} // namespace gte
