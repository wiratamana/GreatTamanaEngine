// Tier-1 tests for BuildRealFrameDebuggerSnapshot(): hand-built render-graph snapshots in, event tree out.
// Every wrapped pass owns one child row and consumes two consecutive event indices.

#include "Editor/FrameDebuggerData.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// Four slots in role order; empty names mean "no image name".
std::vector<FrameDebuggerMaterialSlot> MaterialSlotsOf(const std::string& baseColor, const std::string& normal = {},
    const std::string& metallicRoughness = {}, const std::string& occlusion = {})
{
    return { { "Base Color", baseColor }, { "Normal", normal }, { "Metallic-Roughness", metallicRoughness },
        { "Occlusion", occlusion } };
}

rg::RenderGraphPassSnapshot MakePass(const std::string& name)
{
    rg::RenderGraphPassSnapshot pass;
    pass.name = name;
    return pass;
}

// Compute pass tagged PreOpaques, so a fixture's default-tagged "RenderOpaque" stays the view-region pivot.
rg::RenderGraphPassSnapshot MakeComputePass(const std::string& name)
{
    rg::RenderGraphPassSnapshot pass = MakePass(name);
    pass.kind = rg::PassKind::Compute;
    pass.renderPassEvent = rg::RenderPassEvent::PreOpaques;
    return pass;
}

// Graphics pass with the given category (the replay passes use FrameDebuggerInternal).
rg::RenderGraphPassSnapshot MakeGraphicsPassWithCategory(const std::string& name, rg::RenderPassCategory category)
{
    rg::RenderGraphPassSnapshot pass = MakePass(name);
    pass.category = category;
    return pass;
}

// Blit pass. It keeps the default Opaques event, like any pass declared without a tag.
rg::RenderGraphPassSnapshot MakeBlitPass(const std::string& name)
{
    rg::RenderGraphPassSnapshot pass = MakePass(name);
    pass.kind = rg::PassKind::Blit;
    pass.drawKind = rg::RenderPassDrawKind::Blit;
    return pass;
}

TEST(FrameDebuggerSnapshotBuilderTest, NoRenderOpaquePassProducesEmptyResult)
{
    rg::RenderGraphSnapshot graphSnapshot;
    // Both passes are tagged below Opaques: the pivot lookup ignores names and would otherwise pick one.
    rg::RenderGraphPassSnapshot sceneView = MakePass("SceneView");
    sceneView.renderPassEvent = rg::RenderPassEvent::BeforeEverything;
    graphSnapshot.passesInExecutionOrder.push_back(sceneView);
    rg::RenderGraphPassSnapshot present = MakePass("Present");
    present.renderPassEvent = rg::RenderPassEvent::BeforeEverything;
    graphSnapshot.passesInExecutionOrder.push_back(present);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    EXPECT_TRUE(snapshot.rootNodes.empty());
    EXPECT_EQ(snapshot.totalEventCount, 0);
}

// Only the pivot pass produces a leaf; a secondary-view pass before it is excluded.
TEST(FrameDebuggerSnapshotBuilderTest, RenderOpaqueWithNoComputePassesProducesExactlyOneLeaf)
{
    rg::RenderGraphSnapshot graphSnapshot;
    // Tagged below Opaques and secondary scope, so its exclusion is deliberate and it is not swept up.
    rg::RenderGraphPassSnapshot sceneView = MakePass("SceneView");
    sceneView.renderPassEvent = rg::RenderPassEvent::BeforeEverything;
    sceneView.viewScope = rg::ViewScope::Secondary;
    graphSnapshot.passesInExecutionOrder.push_back(sceneView);
    rg::RenderGraphPassSnapshot renderOpaque = MakePass("RenderOpaque");
    renderOpaque.stats.drawStats.drawCallCount = 7;
    renderOpaque.stats.drawStats.triangleCount = 250;
    graphSnapshot.passesInExecutionOrder.push_back(renderOpaque);

    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("Mesh.vert/Mesh.frag (PositionNormal)", MaterialSlotsOf("MaterialTexture abc123"), Mat4::Identity());

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    EXPECT_FALSE(root.isDrawCall);
    ASSERT_EQ(root.children.size(), 1u); // No compute-dispatch groups at all.
    EXPECT_EQ(snapshot.totalEventCount, 1);

    const FrameDebuggerEventNode& leaf = root.children[0];
    EXPECT_TRUE(leaf.isDrawCall);
    EXPECT_EQ(leaf.eventIndex, 0);
    ASSERT_TRUE(leaf.details.has_value());
    EXPECT_EQ(leaf.details->passName, "RenderOpaque");
    EXPECT_EQ(leaf.details->eventLabel, "Draw Mesh");
    EXPECT_EQ(leaf.details->shaderName, "Mesh.vert/Mesh.frag (PositionNormal)");
    ASSERT_EQ(leaf.details->textures.size(), 1u);
    EXPECT_EQ(leaf.details->textures[0].valueLabel, "MaterialTexture abc123");

    // vectors: clear color + the real aggregate DrawStats of the graph's "RenderOpaque" entry.
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

// Owner groups come first, then the generic group, whatever the execution order.
// Each compute pass owns a "Compute Dispatch" child that consumes one extra event index.
TEST(FrameDebuggerSnapshotBuilderTest, MixedComputeLutAndPreGameViewCategoriesProduceBothGroupsInFixedOrder)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot bufferPass = MakeComputePass("SkinPass_A");
    bufferPass.writeNames.push_back("SkinnedVertexBuffer");
    bufferPass.writeKinds.push_back(rg::ResourceKind::Buffer);
    graphSnapshot.passesInExecutionOrder.push_back(bufferPass);

    rg::RenderGraphPassSnapshot lutPass = MakeComputePass("SkyLutPass");
    lutPass.owningFeatureName = "Compute LUT";
    lutPass.writeNames.push_back("TransmittanceLut");
    lutPass.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(lutPass);

    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // "Compute LUT" group + "Compute Dispatches (Pre-GameView)" group +
    // "RenderOpaque" leaf, siblings, IN THAT ORDER regardless of real
    // execution order.
    ASSERT_EQ(root.children.size(), 3u);
    EXPECT_EQ(snapshot.totalEventCount, 5); // Each wrapped pass now consumes 2 indices (parent + child).

    const FrameDebuggerEventNode& lutGroup = root.children[0];
    EXPECT_FALSE(lutGroup.isDrawCall);
    EXPECT_EQ(lutGroup.name, "Compute LUT");
    ASSERT_EQ(lutGroup.children.size(), 1u);
    EXPECT_EQ(lutGroup.children[0].name, "SkyLutPass");
    // SkyLutPass runs after SkinPass_A's parent and child, so it takes indices 2 and 3.
    EXPECT_EQ(lutGroup.children[0].eventIndex, 2);
    ASSERT_EQ(lutGroup.children[0].children.size(), 1u);
    EXPECT_EQ(lutGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(lutGroup.children[0].children[0].eventIndex, 3);

    const FrameDebuggerEventNode& preGroup = root.children[1];
    EXPECT_FALSE(preGroup.isDrawCall);
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].name, "SkinPass_A");
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);

    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[2];
    EXPECT_TRUE(renderOpaqueLeaf.isDrawCall);
    EXPECT_EQ(renderOpaqueLeaf.name, "RenderOpaque");
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 4); // Sequential across the WHOLE tree.
}

// A pass owned by one feature produces only that feature's group, never an empty generic group.
TEST(FrameDebuggerSnapshotBuilderTest, OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot lutPass = MakeComputePass("AtmosphereTransmittanceLutPass");
    lutPass.owningFeatureName = "Compute LUT";
    graphSnapshot.passesInExecutionOrder.push_back(lutPass);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "Compute LUT" group + "RenderOpaque" leaf, nothing else.
    EXPECT_EQ(root.children[0].name, "Compute LUT");
    EXPECT_EQ(root.children[1].name, "RenderOpaque");
}

// A graphics pass before the pivot is never taken for a compute dispatch; it lands in "Other Render Passes".
TEST(FrameDebuggerSnapshotBuilderTest, NonComputePassIsNeverTreatedAsComputeDispatch)
{
    rg::RenderGraphSnapshot graphSnapshot;
    // Tagged below Opaques so the pivot lookup ignores it.
    rg::RenderGraphPassSnapshot ordinaryGraphicsPass = MakePass("SomeOrdinaryGraphicsPass");
    ordinaryGraphicsPass.renderPassEvent = rg::RenderPassEvent::BeforeEverything;
    graphSnapshot.passesInExecutionOrder.push_back(ordinaryGraphicsPass);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // "RenderOpaque" leaf plus the "Other Render Passes" group, built through the graphics path.
    ASSERT_EQ(root.children.size(), 2u);
    EXPECT_EQ(root.children[0].name, "RenderOpaque");
    const FrameDebuggerEventNode& otherGroup = root.children[1];
    EXPECT_EQ(otherGroup.name, "Other Render Passes");
    ASSERT_EQ(otherGroup.children.size(), 1u);
    const FrameDebuggerEventNode& leaf = otherGroup.children[0];
    EXPECT_EQ(leaf.name, "SomeOrdinaryGraphicsPass");
    ASSERT_TRUE(leaf.details.has_value());
    EXPECT_NE(leaf.details->eventLabel, "Compute Dispatch"); // The actual regression this test guards.
    EXPECT_EQ(leaf.details->eventLabel, "Draw Mesh"); // Default RenderPassDrawKind::DrawMesh.
    EXPECT_EQ(snapshot.totalEventCount, 3); // RenderOpaque(0) + swept leaf parent(1) + child(2).
}

TEST(FrameDebuggerSnapshotBuilderTest, DistinctPipelineAndTextureNamesProduceDistinctEntriesNotDuplicates)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("PipelineA", MaterialSlotsOf("TextureA"), Mat4::Identity());
    capture.RecordDraw("PipelineB", MaterialSlotsOf("TextureB"), Mat4::Identity());
    capture.RecordDraw("PipelineA", MaterialSlotsOf("TextureA"), Mat4::Identity()); // Repeat - must not duplicate.

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

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
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    // Non-symmetric matrix (column c holds 4c+1..4c+4), so a row/column mixup gives visibly wrong values.
    const Mat4 matrix(Vec4(1.0f, 2.0f, 3.0f, 4.0f), Vec4(5.0f, 6.0f, 7.0f, 8.0f), Vec4(9.0f, 10.0f, 11.0f, 12.0f),
        Vec4(13.0f, 14.0f, 15.0f, 16.0f));

    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("Pipeline", {}, matrix);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventDetails& details = *snapshot.rootNodes[0].children[0].details;
    ASSERT_EQ(details.matrices.size(), 1u);
    EXPECT_EQ(details.matrices[0].name, "ViewProjection");
    const std::array<float, 16>& values = details.matrices[0].values;

    // Row-major: values[row*4 + col] == matrix(row, col), so row 0 reads {1, 5, 9, 13}, not column 0.
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
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    FrameDebuggerRenderTargetInfo info;
    info.name = "ThisShouldBeOverwritten";
    info.width = 1920;
    info.height = 1080;
    info.format = "Texture (RGBA8 UNORM)";

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, info);

    // "GameView" is the render texture name, not the pass name.
    EXPECT_EQ(snapshot.renderTarget.name, "GameView");
    EXPECT_EQ(snapshot.renderTarget.width, 1920);
    EXPECT_EQ(snapshot.renderTarget.height, 1080);
    EXPECT_EQ(snapshot.renderTarget.format, "Texture (RGBA8 UNORM)");
}

// A post-pivot compute pass lands in the post group and owns a "Compute Dispatch" child.
TEST(FrameDebuggerSnapshotBuilderTest, PostGameViewComputePassProducesLeafUnderPostGameViewGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    rg::RenderGraphPassSnapshot composite = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    composite.readNames.push_back("GameView");
    composite.readKinds.push_back(rg::ResourceKind::Texture);
    composite.writeNames.push_back("GameViewComposited");
    composite.writeKinds.push_back(rg::ResourceKind::Texture);
    composite.stats.gpu.state = GpuReadState::Present;
    composite.stats.gpu.workMs = 0.25f;
    graphSnapshot.passesInExecutionOrder.push_back(composite);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "RenderOpaque" leaf + new "Compute Dispatches (Post-GameView)" group.
    EXPECT_EQ(snapshot.totalEventCount, 3); // RenderOpaque(0) + composite parent(1) + composite child(2).

    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[0];
    EXPECT_TRUE(renderOpaqueLeaf.isDrawCall);
    EXPECT_EQ(renderOpaqueLeaf.name, "RenderOpaque");
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 0);

    const FrameDebuggerEventNode& postGroup = root.children[1];
    EXPECT_FALSE(postGroup.isDrawCall);
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u);

    const FrameDebuggerEventNode& leaf = postGroup.children[0];
    EXPECT_TRUE(leaf.isDrawCall);
    EXPECT_EQ(leaf.name, "AtmosphereAerialPerspectiveCompositePass");
    EXPECT_EQ(leaf.eventIndex, 1); // Unaffected - the PARENT node's own index is unchanged by wrapping.
    ASSERT_TRUE(leaf.details.has_value());
    // Raw pass name now - no fabricated "Aerial Perspective Composite" label.
    EXPECT_EQ(leaf.details->passName, "AtmosphereAerialPerspectiveCompositePass");
    EXPECT_EQ(leaf.details->eventLabel, "Compute Dispatch"); // Generic label, not the old "Compute Composite".
    // Raw pass name - no hardcoded ".comp" shader filename fabricated anymore.
    EXPECT_EQ(leaf.details->shaderName, "AtmosphereAerialPerspectiveCompositePass");
    EXPECT_EQ(leaf.details->blendMode, "n/a (compute pass)");
    ASSERT_EQ(leaf.details->textures.size(), 2u);
    EXPECT_EQ(leaf.details->textures[0].name, "Read Texture");
    EXPECT_EQ(leaf.details->textures[0].valueLabel, "GameView");
    EXPECT_EQ(leaf.details->textures[1].name, "Write Texture");
    EXPECT_EQ(leaf.details->textures[1].valueLabel, "GameViewComposited");
    ASSERT_EQ(leaf.details->vectors.size(), 1u);
    EXPECT_EQ(leaf.details->vectors[0].name, "GPU Time (ms)");
    EXPECT_FLOAT_EQ(leaf.details->vectors[0].x, 0.25f);

    // The pass owns one child event, "Compute Dispatch".
    ASSERT_EQ(leaf.children.size(), 1u);
    const FrameDebuggerEventNode& child = leaf.children[0];
    EXPECT_EQ(child.name, "Compute Dispatch");
    EXPECT_EQ(child.eventIndex, 2);
    ASSERT_TRUE(child.details.has_value());
    EXPECT_EQ(child.details->eventLabel, "Compute Dispatch");
    EXPECT_EQ(child.details->passName, "AtmosphereAerialPerspectiveCompositePass");
}

// No compute passes produce neither group.
TEST(FrameDebuggerSnapshotBuilderTest, NoComputePassesProduceNeitherGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    EXPECT_EQ(snapshot.rootNodes[0].children.size(), 1u); // Only "RenderOpaque" - no compute group appears.
    EXPECT_EQ(snapshot.totalEventCount, 1);
}

// Pre and post groups appear as siblings of the "RenderOpaque" leaf, in execution order.
TEST(FrameDebuggerSnapshotBuilderTest, PreAndPostGameViewGroupsAppearTogetherAsSiblingsInRealExecutionOrder)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SkinPass_A"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("AtmosphereAerialPerspectiveCompositePass"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    EXPECT_FALSE(preGroup.isDrawCall);
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");

    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[1];
    EXPECT_TRUE(renderOpaqueLeaf.isDrawCall);
    EXPECT_EQ(renderOpaqueLeaf.name, "RenderOpaque");

    const FrameDebuggerEventNode& postGroup = root.children[2];
    EXPECT_FALSE(postGroup.isDrawCall);
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");

    // eventIndex values strictly increasing left-to-right across the whole
    // tree: SkinPass_A(0) + its own child "Compute Dispatch"(1), RenderOpaque(2),
    // AtmosphereAerialPerspectiveCompositePass(3) + its own child "Compute Dispatch"(4).
    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_EQ(postGroup.children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 2);
    EXPECT_EQ(postGroup.children[0].eventIndex, 3);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 4);
    EXPECT_EQ(snapshot.totalEventCount, 5);
}

// A secondary-view pass sharing a name with a primary-view pass is excluded.
TEST(FrameDebuggerSnapshotBuilderTest, SceneViewScopedPreGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot gameViewScoped = MakeComputePass("AtmosphereSkyViewLutPass");
    gameViewScoped.viewScope = rg::ViewScope::Primary;
    graphSnapshot.passesInExecutionOrder.push_back(gameViewScoped);

    rg::RenderGraphPassSnapshot sceneViewScoped = MakeComputePass("AtmosphereSkyViewLutPass");
    sceneViewScoped.viewScope = rg::ViewScope::Secondary;
    graphSnapshot.passesInExecutionOrder.push_back(sceneViewScoped);

    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "Compute Dispatches (Pre-GameView)" group + "RenderOpaque" leaf.

    const FrameDebuggerEventNode& preGroup = root.children[0];
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u); // NOT two - the SceneView-scoped duplicate must be gone.
    EXPECT_EQ(preGroup.children[0].name, "AtmosphereSkyViewLutPass");
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(snapshot.totalEventCount, 3); // Wrapped pass (parent+child) + RenderOpaque.
}

// Same exclusion for the post group.
TEST(FrameDebuggerSnapshotBuilderTest, SceneViewScopedPostGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    rg::RenderGraphPassSnapshot gameViewScoped = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    gameViewScoped.viewScope = rg::ViewScope::Primary;
    graphSnapshot.passesInExecutionOrder.push_back(gameViewScoped);

    rg::RenderGraphPassSnapshot sceneViewScoped = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    sceneViewScoped.viewScope = rg::ViewScope::Secondary;
    graphSnapshot.passesInExecutionOrder.push_back(sceneViewScoped);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "RenderOpaque" leaf + "Compute Dispatches (Post-GameView)" group.

    const FrameDebuggerEventNode& postGroup = root.children[1];
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u); // NOT two - the SceneView-scoped duplicate must be gone.
    EXPECT_EQ(postGroup.children[0].name, "AtmosphereAerialPerspectiveCompositePass");
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(snapshot.totalEventCount, 3); // RenderOpaque + wrapped pass (parent+child).
}

// Shared-scope passes still appear.
TEST(FrameDebuggerSnapshotBuilderTest, SharedViewScopedPassStillAppearsNormally)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot shared = MakeComputePass("AtmosphereTransmittanceLutPass");
    shared.viewScope = rg::ViewScope::Shared; // Explicit, though this is also the default.
    graphSnapshot.passesInExecutionOrder.push_back(shared);

    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u);
    const FrameDebuggerEventNode& preGroup = root.children[0];
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].name, "AtmosphereTransmittanceLutPass");
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(snapshot.totalEventCount, 3);
}

// "RenderOpaque" gets one child leaf per draw record.
TEST(FrameDebuggerSnapshotBuilderTest, RenderOpaqueNodeGetsOneChildLeafPerDrawRecord)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", {}, 1045458);
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", MaterialSlotsOf("MaterialTexture abc123"), 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 1u); // Only "RenderOpaque" - no compute-dispatch groups.

    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[0];
    EXPECT_TRUE(renderOpaqueLeaf.isDrawCall);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 0);
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 2u); // (a) exactly that many children.

    // (b) each child's name/eventIndex is correct.
    const FrameDebuggerEventNode& terrainLeaf = renderOpaqueLeaf.children[0];
    EXPECT_EQ(terrainLeaf.name, "terrain (Entity 2)");
    EXPECT_TRUE(terrainLeaf.isDrawCall);
    EXPECT_EQ(terrainLeaf.eventIndex, 1);
    ASSERT_TRUE(terrainLeaf.details.has_value());
    EXPECT_EQ(terrainLeaf.details->shaderName, "Mesh.vert/Mesh.frag (PositionNormal)");
    EXPECT_TRUE(terrainLeaf.details->textures.empty()); // Untextured draw - no material rows.
    bool foundTerrainTriangleCount = false;
    for (const FrameDebuggerVectorProperty& vec : terrainLeaf.details->vectors) {
        if (vec.name == "Triangle Count") {
            foundTerrainTriangleCount = true;
            EXPECT_FLOAT_EQ(vec.x, 1045458.0f); // This ONE draw's own count, never the whole-pass aggregate.
        }
    }
    EXPECT_TRUE(foundTerrainTriangleCount);

    const FrameDebuggerEventNode& cubeLeaf = renderOpaqueLeaf.children[1];
    EXPECT_EQ(cubeLeaf.name, "SmokeTestCube (Entity 3)");
    EXPECT_EQ(cubeLeaf.eventIndex, 2);
    ASSERT_TRUE(cubeLeaf.details.has_value());
    ASSERT_EQ(cubeLeaf.details->textures.size(), 4u); // One row per material slot.
    EXPECT_EQ(cubeLeaf.details->textures[0].name, "Base Color Texture");
    EXPECT_EQ(cubeLeaf.details->textures[0].valueLabel, "MaterialTexture abc123");
    EXPECT_EQ(cubeLeaf.details->textures[1].valueLabel, "None");

    // (c) eventIndex stays strictly monotonic increasing: RenderOpaque(0) < terrain(1) < cube(2).
    EXPECT_LT(renderOpaqueLeaf.eventIndex, terrainLeaf.eventIndex);
    EXPECT_LT(terrainLeaf.eventIndex, cubeLeaf.eventIndex);

    // (d) totalEventCount correctly includes the new children.
    EXPECT_EQ(snapshot.totalEventCount, 3);
}

// Event indices increase across the pre group, the pivot, its children and the post group.
TEST(FrameDebuggerSnapshotBuilderTest, EventIndexIsMonotonicAcrossPreGameViewRenderOpaqueChildrenAndPostGameView)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostA"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", {}, 1045458);
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", {}, 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[1];
    const FrameDebuggerEventNode& postGroup = root.children[2];

    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 2u);
    ASSERT_EQ(postGroup.children.size(), 1u);

    // PreA parent(0) + its own child "Compute Dispatch"(1), RenderOpaque(2),
    // its two entity children(3, 4), PostA parent(5) + its own child(6).
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 2);
    EXPECT_EQ(renderOpaqueLeaf.children[0].eventIndex, 3);
    EXPECT_EQ(renderOpaqueLeaf.children[1].eventIndex, 4);
    EXPECT_EQ(postGroup.children[0].eventIndex, 5);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 6);
    EXPECT_EQ(snapshot.totalEventCount, 7);
}

// Without draw records "RenderOpaque" has no children.
TEST(FrameDebuggerSnapshotBuilderTest, RenderOpaqueNodeHasNoChildrenWhenNoDrawRecordsCaptured)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    EXPECT_TRUE(renderOpaqueLeaf.children.empty());
    EXPECT_EQ(snapshot.totalEventCount, 1);
}

// A per-entity child's passName is never the literal "RenderOpaque"; only the pass leaf carries it.
TEST(FrameDebuggerSnapshotBuilderTest, RenderOpaqueDrawRecordLeafPassNameIsNeverLiterallyRenderOpaque)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", {}, 1045458);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 1u);
    ASSERT_TRUE(renderOpaqueLeaf.children[0].details.has_value());
    EXPECT_NE(renderOpaqueLeaf.children[0].details->passName, "RenderOpaque");
    // Also confirm the real "RenderOpaque" pass leaf itself is unaffected -
    // it must still carry the literal value, unchanged.
    ASSERT_TRUE(renderOpaqueLeaf.details.has_value());
    EXPECT_EQ(renderOpaqueLeaf.details->passName, "RenderOpaque");
}

// Culled compute passes appear in neither group and consume no event index.
TEST(FrameDebuggerSnapshotBuilderTest, CulledComputePassIsExcludedFromEitherComputeDispatchGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot culledPre = MakeComputePass("CulledPreComputePass");
    culledPre.isCulled = true;
    graphSnapshot.passesInExecutionOrder.push_back(culledPre);

    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SurvivingPreComputePass"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SurvivingPostComputePass"));

    rg::RenderGraphPassSnapshot culledPost = MakeComputePass("CulledPostComputePass");
    culledPost.isCulled = true;
    graphSnapshot.passesInExecutionOrder.push_back(culledPost);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    ASSERT_EQ(preGroup.children.size(), 1u); // The culled pre pass never appears.
    EXPECT_EQ(preGroup.children[0].name, "SurvivingPreComputePass");
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");

    const FrameDebuggerEventNode& postGroup = root.children[2];
    ASSERT_EQ(postGroup.children.size(), 1u); // The culled post pass never appears.
    EXPECT_EQ(postGroup.children[0].name, "SurvivingPostComputePass");
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].name, "Compute Dispatch");

    // SurvivingPre parent(0) + child(1), RenderOpaque(2), SurvivingPost
    // parent(3) + child(4) - only 5 real events; a culled pass consumes
    // NO index at all, wrapped or not.
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(root.children[1].eventIndex, 2);
    EXPECT_EQ(postGroup.children[0].eventIndex, 3);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 4);
    EXPECT_EQ(snapshot.totalEventCount, 5); // Only 5 real events (2 wrapped passes + RenderOpaque).
}

// Read and write rows are labeled by their resource kind.
TEST(FrameDebuggerSnapshotBuilderTest, ComputeDispatchLeafLabelsReadWriteRowsByRealResourceKind)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot pass = MakeComputePass("MixedKindComputePass");
    pass.readNames = { "SomeTexture", "SomeBuffer", "SomeVolume" };
    pass.readKinds
        = { rg::ResourceKind::Texture, rg::ResourceKind::Buffer, rg::ResourceKind::VolumeTexture };
    pass.writeNames = { "OutputTexture", "OutputBuffer", "OutputVolume" };
    pass.writeKinds
        = { rg::ResourceKind::Texture, rg::ResourceKind::Buffer, rg::ResourceKind::VolumeTexture };
    graphSnapshot.passesInExecutionOrder.push_back(pass);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    ASSERT_EQ(snapshot.rootNodes[0].children.size(), 2u);
    const FrameDebuggerEventNode& preGroup = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_TRUE(preGroup.children[0].details.has_value());
    const FrameDebuggerEventDetails& details = *preGroup.children[0].details;

    ASSERT_EQ(details.textures.size(), 6u);
    EXPECT_EQ(details.textures[0].name, "Read Texture");
    EXPECT_EQ(details.textures[0].valueLabel, "SomeTexture");
    // Each row also carries its kind and the render-graph flag.
    EXPECT_EQ(details.textures[0].kind, rg::ResourceKind::Texture);
    EXPECT_TRUE(details.textures[0].isRenderGraphResource);
    EXPECT_EQ(details.textures[1].name, "Read Buffer");
    EXPECT_EQ(details.textures[1].valueLabel, "SomeBuffer");
    EXPECT_EQ(details.textures[1].kind, rg::ResourceKind::Buffer);
    EXPECT_TRUE(details.textures[1].isRenderGraphResource);
    EXPECT_EQ(details.textures[2].name, "Read Volume Texture");
    EXPECT_EQ(details.textures[2].valueLabel, "SomeVolume");
    EXPECT_EQ(details.textures[2].kind, rg::ResourceKind::VolumeTexture);
    EXPECT_TRUE(details.textures[2].isRenderGraphResource);
    EXPECT_EQ(details.textures[3].name, "Write Texture");
    EXPECT_EQ(details.textures[3].valueLabel, "OutputTexture");
    EXPECT_EQ(details.textures[3].kind, rg::ResourceKind::Texture);
    EXPECT_TRUE(details.textures[3].isRenderGraphResource);
    EXPECT_EQ(details.textures[4].name, "Write Buffer");
    EXPECT_EQ(details.textures[4].valueLabel, "OutputBuffer");
    EXPECT_EQ(details.textures[4].kind, rg::ResourceKind::Buffer);
    EXPECT_TRUE(details.textures[4].isRenderGraphResource);
    EXPECT_EQ(details.textures[5].name, "Write Volume Texture");
    EXPECT_EQ(details.textures[5].valueLabel, "OutputVolume");
    EXPECT_EQ(details.textures[5].kind, rg::ResourceKind::VolumeTexture);
    EXPECT_TRUE(details.textures[5].isRenderGraphResource);
}

// Event indices increase in execution order: pre group, then the pivot, then the post group.
TEST(FrameDebuggerSnapshotBuilderTest, EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreB"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostB"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[1];
    const FrameDebuggerEventNode& postGroup = root.children[2];

    ASSERT_EQ(preGroup.children.size(), 2u);
    ASSERT_EQ(postGroup.children.size(), 2u);

    // PreA parent(0)+child(1), PreB parent(2)+child(3), RenderOpaque(4),
    // PostA parent(5)+child(6), PostB parent(7)+child(8).
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(preGroup.children[1].eventIndex, 2);
    ASSERT_EQ(preGroup.children[1].children.size(), 1u);
    EXPECT_EQ(preGroup.children[1].children[0].eventIndex, 3);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 4);
    EXPECT_EQ(postGroup.children[0].eventIndex, 5);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 6);
    EXPECT_EQ(postGroup.children[1].eventIndex, 7);
    ASSERT_EQ(postGroup.children[1].children.size(), 1u);
    EXPECT_EQ(postGroup.children[1].children[0].eventIndex, 8);
    EXPECT_EQ(snapshot.totalEventCount, 9);
}

// Step preview kinds: a pre-pivot leaf, the pivot leaf, per-object children, and post leaves
// before and after the composite pass.

TEST(FrameDebuggerSnapshotBuilderTest, PreGameViewLeafGetsNotYetDrawnStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SkinPass_A"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& preGroup = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_TRUE(preGroup.children[0].details.has_value());
    EXPECT_EQ(preGroup.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::NotYetDrawn);
}

TEST(FrameDebuggerSnapshotBuilderTest, RenderOpaqueLeafGetsPreCompositeStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_TRUE(renderOpaqueLeaf.details.has_value());
    EXPECT_EQ(renderOpaqueLeaf.details->stepPreviewKind, FrameDebuggerStepPreviewKind::PreComposite);
}

TEST(FrameDebuggerSnapshotBuilderTest, PerObjectDrawRecordChildrenGetPerObjectStepKindAndIncreasingIndex)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", {}, 1045458);
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", MaterialSlotsOf("MaterialTexture abc123"), 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 2u);

    ASSERT_TRUE(renderOpaqueLeaf.children[0].details.has_value());
    EXPECT_EQ(renderOpaqueLeaf.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PerObjectStep);
    EXPECT_EQ(renderOpaqueLeaf.children[0].details->stepPreviewIndex, 0);

    ASSERT_TRUE(renderOpaqueLeaf.children[1].details.has_value());
    EXPECT_EQ(renderOpaqueLeaf.children[1].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PerObjectStep);
    EXPECT_EQ(renderOpaqueLeaf.children[1].details->stepPreviewIndex, 1);
}

TEST(FrameDebuggerSnapshotBuilderTest, PostGameViewLeafBeforeCompositePassGetsPreCompositeStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SomeOtherPostGameViewPass"));

    rg::RenderGraphPassSnapshot composite = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    composite.writeNames.push_back("GameViewComposited");
    composite.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(composite);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& postGroup = snapshot.rootNodes[0].children[1];
    ASSERT_EQ(postGroup.children.size(), 2u);
    ASSERT_TRUE(postGroup.children[0].details.has_value());
    EXPECT_EQ(postGroup.children[0].name, "SomeOtherPostGameViewPass");
    EXPECT_EQ(postGroup.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PreComposite);
}

TEST(FrameDebuggerSnapshotBuilderTest, CompositePassLeafItselfGetsPostCompositeStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    rg::RenderGraphPassSnapshot composite = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    composite.writeNames.push_back("GameViewComposited");
    composite.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(composite);

    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SomeLaterPostCompositePass"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& postGroup = snapshot.rootNodes[0].children[1];
    ASSERT_EQ(postGroup.children.size(), 2u);

    ASSERT_TRUE(postGroup.children[0].details.has_value());
    EXPECT_EQ(postGroup.children[0].name, "AtmosphereAerialPerspectiveCompositePass");
    EXPECT_EQ(postGroup.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PostComposite);

    // A leaf strictly AFTER the composite pass is ALSO PostComposite.
    ASSERT_TRUE(postGroup.children[1].details.has_value());
    EXPECT_EQ(postGroup.children[1].name, "SomeLaterPostCompositePass");
    EXPECT_EQ(postGroup.children[1].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PostComposite);
}

TEST(FrameDebuggerSnapshotBuilderTest, PostGameViewLeavesDefaultToPostCompositeWhenNoCompositePassSurvives)
{
    // No pass writes "GameViewComposited": every post leaf defaults to PostComposite.
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SomePostGameViewPassWithNoCompositeWrite"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& postGroup = snapshot.rootNodes[0].children[1];
    ASSERT_EQ(postGroup.children.size(), 1u);
    ASSERT_TRUE(postGroup.children[0].details.has_value());
    EXPECT_EQ(postGroup.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PostComposite);
}

// "DrawSkyBackground" is a separate view-region pass and composes with the pre/post compute split.
// Each wrapped pass owns a child row; "RenderOpaque" keeps its per-entity children.
TEST(FrameDebuggerSnapshotBuilderTest, DrawSkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass);
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostA"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", {}, 1045458);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 4u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[1];
    const FrameDebuggerEventNode& skyLeaf = root.children[2];
    const FrameDebuggerEventNode& postGroup = root.children[3];

    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u);

    EXPECT_EQ(renderOpaqueLeaf.name, "RenderOpaque");
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 1u); // Only the entity - sky is its own sibling leaf now.
    const FrameDebuggerEventNode& entityLeaf = renderOpaqueLeaf.children[0];
    EXPECT_EQ(entityLeaf.name, "terrain (Entity 2)");

    EXPECT_EQ(skyLeaf.name, "DrawSkyBackground");
    EXPECT_TRUE(skyLeaf.isDrawCall);
    ASSERT_EQ(skyLeaf.children.size(), 1u); // NOW owns exactly one real child event.
    ASSERT_TRUE(skyLeaf.details.has_value());
    EXPECT_EQ(skyLeaf.details->passName, "DrawSkyBackground");
    EXPECT_EQ(skyLeaf.details->shaderName, "DrawSkyBackground");
    EXPECT_EQ(skyLeaf.details->zTest, "Equal");
    EXPECT_EQ(skyLeaf.details->zWrite, "Off");
    EXPECT_EQ(skyLeaf.details->stepPreviewKind, FrameDebuggerStepPreviewKind::PreComposite);

    const FrameDebuggerEventNode& skyChild = skyLeaf.children[0];
    EXPECT_EQ(skyChild.name, "Draw Quad"); // The sky pass is tagged RenderPassDrawKind::DrawQuad.
    ASSERT_TRUE(skyChild.details.has_value());
    EXPECT_EQ(skyChild.details->eventLabel, "Draw Quad");
    EXPECT_EQ(skyChild.details->passName, "DrawSkyBackground"); // Same owning pass.

    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].name, "Compute Dispatch");

    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");

    // eventIndex strictly monotonic, now 8 total leaves (each wrapped pass
    // contributes one parent + one child).
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 2);
    EXPECT_EQ(entityLeaf.eventIndex, 3);
    EXPECT_EQ(skyLeaf.eventIndex, 4);
    EXPECT_EQ(skyChild.eventIndex, 5);
    EXPECT_EQ(postGroup.children[0].eventIndex, 6);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 7);
    EXPECT_EQ(snapshot.totalEventCount, 8);
}

// The shared "GameView" buffer gives no private write texture; a private resource does.
TEST(FrameDebuggerSnapshotBuilderTest, PrivateWriteTextureNameExcludesGameViewButPopulatesForPrivateResource)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot lutPass = MakeComputePass("SkyLutPass");
    lutPass.writeNames.push_back("TransmittanceLut");
    lutPass.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(lutPass);

    rg::RenderGraphPassSnapshot renderOpaque = MakePass("RenderOpaque");
    renderOpaque.writeNames.push_back("GameView");
    renderOpaque.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(renderOpaque);

    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    skyPass.writeNames.push_back("GameView");
    skyPass.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(skyPass);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u); // "Compute Dispatches (Pre-GameView)" group + RenderOpaque + DrawSkyBackground.

    const FrameDebuggerEventNode& preGroup = root.children[0];
    ASSERT_EQ(preGroup.children.size(), 1u);
    const FrameDebuggerEventNode& lutLeaf = preGroup.children[0];
    ASSERT_TRUE(lutLeaf.details.has_value());
    ASSERT_TRUE(lutLeaf.details->privateWriteTextureName.has_value());
    EXPECT_EQ(*lutLeaf.details->privateWriteTextureName, "TransmittanceLut");

    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[1];
    ASSERT_TRUE(renderOpaqueLeaf.details.has_value());
    EXPECT_FALSE(renderOpaqueLeaf.details->privateWriteTextureName.has_value());

    const FrameDebuggerEventNode& skyLeaf = root.children[2];
    ASSERT_TRUE(skyLeaf.details.has_value());
    EXPECT_FALSE(skyLeaf.details->privateWriteTextureName.has_value());
}

// Without a "RenderTransparent" pass the view region is exactly "RenderOpaque" and "DrawSkyBackground".
// "DrawSkyBackground" owns a "Draw Quad" child.
TEST(FrameDebuggerSnapshotBuilderTest, ViewRegionHasExactlyRenderOpaqueAndDrawSkyBackgroundWhenRenderTransparentAbsent)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // Exactly "RenderOpaque" + "DrawSkyBackground" - nothing else.
    EXPECT_EQ(root.children[0].name, "RenderOpaque");
    EXPECT_EQ(root.children[1].name, "DrawSkyBackground");

    ASSERT_EQ(root.children[1].children.size(), 1u);
    EXPECT_EQ(root.children[1].children[0].name, "Draw Quad");
}

// FrameDebuggerInternal replay passes inside the view region never produce a leaf.
TEST(FrameDebuggerSnapshotBuilderTest, FrameDebuggerInternalCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass);
    graphSnapshot.passesInExecutionOrder.push_back(
        MakeGraphicsPassWithCategory("FrameDebuggerReplayStep0", rg::RenderPassCategory::FrameDebuggerInternal));
    graphSnapshot.passesInExecutionOrder.push_back(
        MakeGraphicsPassWithCategory("FrameDebuggerReplayStep1", rg::RenderPassCategory::FrameDebuggerInternal));

    rg::RenderGraphPassSnapshot composite = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    composite.writeNames.push_back("GameViewComposited");
    composite.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(composite);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // The replay passes produce no leaves, not even in "Other Render Passes".
    ASSERT_EQ(root.children.size(), 3u);
    EXPECT_EQ(root.children[0].name, "RenderOpaque");
    EXPECT_EQ(root.children[1].name, "DrawSkyBackground");
    const FrameDebuggerEventNode& postGroup = root.children[2];
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].name, "AtmosphereAerialPerspectiveCompositePass");

    // Event indices skip the internal passes: RenderOpaque(0), sky(1) + child(2), composite(3) + child(4).
    EXPECT_EQ(root.children[0].eventIndex, 0);
    EXPECT_EQ(root.children[1].eventIndex, 1);
    ASSERT_EQ(root.children[1].children.size(), 1u);
    EXPECT_EQ(root.children[1].children[0].eventIndex, 2);
    EXPECT_EQ(root.children[1].children[0].name, "Draw Quad");
    EXPECT_EQ(postGroup.children[0].eventIndex, 3);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 4);
    EXPECT_EQ(snapshot.totalEventCount, 5);
}

// A material slot row has no render-graph entry, so the panel must never draw a "View" button for it.
TEST(FrameDebuggerSnapshotBuilderTest, MaterialSlotRowIsNeverARenderGraphResource)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)",
        MaterialSlotsOf("MaterialTexture abc123"), 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 1u);
    ASSERT_TRUE(renderOpaqueLeaf.children[0].details.has_value());
    const auto& textures = renderOpaqueLeaf.children[0].details->textures;
    ASSERT_EQ(textures.size(), 4u);
    EXPECT_EQ(textures[0].name, "Base Color Texture");
    for (const FrameDebuggerTextureProperty& texture : textures) {
        EXPECT_FALSE(texture.isRenderGraphResource);
    }
}

// Four named slots become four rows in role order; an unnamed slot reads "None".
TEST(FrameDebuggerSnapshotBuilderTest, EntityDrawListsOneRowPerMaterialSlotInRoleOrder)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(1, 0, "wall", "PbrMesh", MaterialSlotsOf("Base A", "Normal A", "MR A", ""), 10);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);
    const auto& textures = snapshot.rootNodes[0].children[0].children[0].details->textures;

    ASSERT_EQ(textures.size(), 4u);
    EXPECT_EQ(textures[0].name, "Base Color Texture");
    EXPECT_EQ(textures[0].valueLabel, "Base A");
    EXPECT_EQ(textures[1].name, "Normal Texture");
    EXPECT_EQ(textures[1].valueLabel, "Normal A");
    EXPECT_EQ(textures[2].name, "Metallic-Roughness Texture");
    EXPECT_EQ(textures[2].valueLabel, "MR A");
    EXPECT_EQ(textures[3].name, "Occlusion Texture");
    EXPECT_EQ(textures[3].valueLabel, "None");
}

// Two draws sharing a normal image list it once in the RenderOpaque leaf and in both entity leaves.
TEST(FrameDebuggerSnapshotBuilderTest, SharedSlotImageIsListedOnceInTheRenderOpaqueLeafAndInEachEntityLeaf)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    const auto first = MaterialSlotsOf("Base A", "Normal Shared");
    const auto second = MaterialSlotsOf("Base B", "Normal Shared");
    capture.RecordDraw("PbrMesh", first, Mat4::Identity());
    capture.RecordDraw("PbrMesh", second, Mat4::Identity());
    capture.RecordEntityDraw(1, 0, "a", "PbrMesh", first, 10);
    capture.RecordEntityDraw(2, 0, "b", "PbrMesh", second, 10);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];

    int sharedRows = 0;
    for (const FrameDebuggerTextureProperty& texture : renderOpaqueLeaf.details->textures) {
        sharedRows += texture.valueLabel == "Normal Shared" ? 1 : 0;
    }
    EXPECT_EQ(sharedRows, 1);
    EXPECT_EQ(renderOpaqueLeaf.details->textures.size(), 3u); // Base A, Normal Shared, Base B.

    ASSERT_EQ(renderOpaqueLeaf.children.size(), 2u);
    for (const FrameDebuggerEventNode& entityLeaf : renderOpaqueLeaf.children) {
        EXPECT_EQ(entityLeaf.details->textures[1].valueLabel, "Normal Shared");
    }
}

// Parent and child rows of a wrapped graphics pass and a wrapped compute pass both resolve
// through FindEventDetailsByIndex().
TEST(FrameDebuggerSnapshotBuilderTest, WrappedPassLevelNodeAndItsChildAreBothIndependentlySelectable)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SomeComputePass"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    // SomeComputePass parent(0) + child(1), RenderOpaque(2), DrawSkyBackground
    // parent(3) + child(4).
    ASSERT_EQ(snapshot.totalEventCount, 5);

    // The wrapped GRAPHICS-kind pass ("DrawSkyBackground") - both its parent
    // row and its owned child row are independently resolvable via the real
    // lookup function, not just by inspecting the tree directly.
    const std::optional<FrameDebuggerEventDetails> skyParentDetails = FindEventDetailsByIndex(snapshot, 3);
    ASSERT_TRUE(skyParentDetails.has_value());
    EXPECT_EQ(skyParentDetails->passName, "DrawSkyBackground");

    const std::optional<FrameDebuggerEventDetails> skyChildDetails = FindEventDetailsByIndex(snapshot, 4);
    ASSERT_TRUE(skyChildDetails.has_value());
    EXPECT_EQ(skyChildDetails->passName, "DrawSkyBackground");
    EXPECT_EQ(skyChildDetails->eventLabel, "Draw Quad");

    // The same holds for a wrapped compute pass.
    const std::optional<FrameDebuggerEventDetails> computeParentDetails = FindEventDetailsByIndex(snapshot, 0);
    ASSERT_TRUE(computeParentDetails.has_value());
    EXPECT_EQ(computeParentDetails->passName, "SomeComputePass");

    const std::optional<FrameDebuggerEventDetails> computeChildDetails = FindEventDetailsByIndex(snapshot, 1);
    ASSERT_TRUE(computeChildDetails.has_value());
    EXPECT_EQ(computeChildDetails->passName, "SomeComputePass");
    EXPECT_EQ(computeChildDetails->eventLabel, "Compute Dispatch");
}

// The owned child's label follows the structural draw kind, never the pass name.
TEST(FrameDebuggerSnapshotBuilderTest, GraphicsChildEventLabelMatchesRenderPassDrawKind)
{
    // DrawQuad.
    {
        rg::RenderGraphSnapshot graphSnapshot;
        graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
        rg::RenderGraphPassSnapshot quadPass = MakePass("SyntheticQuadPass");
        quadPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
        graphSnapshot.passesInExecutionOrder.push_back(quadPass);

        const FrameDebuggerCaptureContext capture;
        const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

        ASSERT_EQ(snapshot.rootNodes.size(), 1u);
        ASSERT_EQ(snapshot.rootNodes[0].children.size(), 2u);
        const FrameDebuggerEventNode& passLeaf = snapshot.rootNodes[0].children[1];
        EXPECT_EQ(passLeaf.name, "SyntheticQuadPass");
        ASSERT_EQ(passLeaf.children.size(), 1u);
        EXPECT_EQ(passLeaf.children[0].name, "Draw Quad");
    }

    // DrawMesh.
    {
        rg::RenderGraphSnapshot graphSnapshot;
        graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
        rg::RenderGraphPassSnapshot meshPass = MakePass("SyntheticMeshPass");
        meshPass.drawKind = rg::RenderPassDrawKind::DrawMesh;
        graphSnapshot.passesInExecutionOrder.push_back(meshPass);

        const FrameDebuggerCaptureContext capture;
        const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

        ASSERT_EQ(snapshot.rootNodes.size(), 1u);
        ASSERT_EQ(snapshot.rootNodes[0].children.size(), 2u);
        const FrameDebuggerEventNode& passLeaf = snapshot.rootNodes[0].children[1];
        EXPECT_EQ(passLeaf.name, "SyntheticMeshPass");
        ASSERT_EQ(passLeaf.children.size(), 1u);
        EXPECT_EQ(passLeaf.children[0].name, "Draw Mesh");
    }

    // Blit.
    {
        rg::RenderGraphSnapshot graphSnapshot;
        graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
        rg::RenderGraphPassSnapshot blitPass = MakePass("SyntheticBlitPass");
        blitPass.drawKind = rg::RenderPassDrawKind::Blit;
        graphSnapshot.passesInExecutionOrder.push_back(blitPass);

        const FrameDebuggerCaptureContext capture;
        const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

        ASSERT_EQ(snapshot.rootNodes.size(), 1u);
        ASSERT_EQ(snapshot.rootNodes[0].children.size(), 2u);
        const FrameDebuggerEventNode& passLeaf = snapshot.rootNodes[0].children[1];
        EXPECT_EQ(passLeaf.name, "SyntheticBlitPass");
        ASSERT_EQ(passLeaf.children.size(), 1u);
        EXPECT_EQ(passLeaf.children[0].name, "Blit");
    }
}

// A "Compute LUT" sub-pass also owns a real "Compute Dispatch" child
// event, exactly like every other wrapped compute pass.
TEST(FrameDebuggerSnapshotBuilderTest, ComputeLutSubPassAlsoOwnsAComputeDispatchChild)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot lutPass = MakeComputePass("AtmosphereTransmittanceLutPass");
    lutPass.owningFeatureName = "Compute LUT";
    graphSnapshot.passesInExecutionOrder.push_back(lutPass);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "Compute LUT" group + "RenderOpaque" leaf, nothing else.
    const FrameDebuggerEventNode& lutGroup = root.children[0];
    EXPECT_EQ(lutGroup.name, "Compute LUT");
    ASSERT_EQ(lutGroup.children.size(), 1u);
    ASSERT_EQ(lutGroup.children[0].children.size(), 1u);
    EXPECT_EQ(lutGroup.children[0].children[0].name, "Compute Dispatch");
}

// Confirms a pass's owningFeatureName string alone drives its sidebar
// group - BuildRealFrameDebuggerSnapshot() never hardcodes any feature name.
TEST(FrameDebuggerSnapshotBuilderTest, ASyntheticThirdPartyTagAndHeadingGetsGroupedWithZeroProductionCodeAwareness)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot fakePluginPass = MakeComputePass("SyntheticFuturePluginPass");
    fakePluginPass.owningFeatureName = "Totally Fake Future Plugin Group";
    graphSnapshot.passesInExecutionOrder.push_back(fakePluginPass);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // "Totally Fake Future Plugin Group" + "RenderOpaque" leaf, nothing else.
    ASSERT_EQ(root.children.size(), 2u);
    const FrameDebuggerEventNode& fakeGroup = root.children[0];
    EXPECT_FALSE(fakeGroup.isDrawCall);
    EXPECT_EQ(fakeGroup.name, "Totally Fake Future Plugin Group");
    ASSERT_EQ(fakeGroup.children.size(), 1u);
    EXPECT_EQ(fakeGroup.children[0].name, "SyntheticFuturePluginPass");
    EXPECT_EQ(root.children[1].name, "RenderOpaque");
}

// The pivot is found by event tag, not by name: a pass named "RenderOpaque" tagged below Opaques is not the pivot.
TEST(FrameDebuggerSnapshotBuilderTest, ViewRegionPivotIsFoundStructurallyEvenWhenNotNamedRenderOpaque)
{
    rg::RenderGraphSnapshot graphSnapshot;

    // A red herring: literally named "RenderOpaque", but tagged BELOW
    // Opaques - must NOT be picked as the pivot, proving the lookup never
    // special-cases this literal string either.
    rg::RenderGraphPassSnapshot redHerring = MakePass("RenderOpaque");
    redHerring.renderPassEvent = rg::RenderPassEvent::BeforeEverything;
    graphSnapshot.passesInExecutionOrder.push_back(redHerring);

    // The REAL pivot - a differently-named pass, tagged Opaques (the value
    // FindViewRegionPivot() actually keys off of).
    rg::RenderGraphPassSnapshot renamedOpaquePass = MakePass("MainOpaqueDrawPass");
    renamedOpaquePass.renderPassEvent = rg::RenderPassEvent::Opaques;
    renamedOpaquePass.stats.drawStats.drawCallCount = 3;
    renamedOpaquePass.stats.drawStats.triangleCount = 99;
    graphSnapshot.passesInExecutionOrder.push_back(renamedOpaquePass);

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(5, 0, "renamed-pivot-proof", "Mesh.vert/Mesh.frag (PositionNormal)", {}, 42);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // The red herring is a surviving pass before the pivot, so it lands in "Other Render Passes".
    ASSERT_EQ(root.children.size(), 2u);

    const FrameDebuggerEventNode& leaf = root.children[0];
    EXPECT_TRUE(leaf.isDrawCall);
    ASSERT_TRUE(leaf.details.has_value());
    // BuildRenderOpaqueLeaf() always uses the literal "RenderOpaque" label; the data must be the real pivot's.
    bool foundDrawStats = false;
    for (const FrameDebuggerVectorProperty& vec : leaf.details->vectors) {
        if (vec.name == "Draw Stats (Calls, Tris)") {
            foundDrawStats = true;
            EXPECT_FLOAT_EQ(vec.x, 3.0f);
            EXPECT_FLOAT_EQ(vec.y, 99.0f);
        }
    }
    EXPECT_TRUE(foundDrawStats);

    ASSERT_EQ(leaf.children.size(), 1u);
    EXPECT_EQ(leaf.children[0].name, "renamed-pivot-proof (Entity 5)");

    // The red herring itself - swept up, but never confused with the real
    // pivot: its own real draw stats are all default/zero.
    const FrameDebuggerEventNode& otherGroup = root.children[1];
    EXPECT_EQ(otherGroup.name, "Other Render Passes");
    ASSERT_EQ(otherGroup.children.size(), 1u);
    const FrameDebuggerEventNode& redHerringLeaf = otherGroup.children[0];
    EXPECT_EQ(redHerringLeaf.name, "RenderOpaque"); // Its own literal, real (misleading) name - never fabricated.
    ASSERT_TRUE(redHerringLeaf.details.has_value());
    bool redHerringHasNonZeroDrawStats = false;
    for (const FrameDebuggerVectorProperty& vec : redHerringLeaf.details->vectors) {
        if (vec.name == "Draw Stats (Calls, Tris)" && (vec.x != 0.0f || vec.y != 0.0f)) {
            redHerringHasNonZeroDrawStats = true;
        }
    }
    EXPECT_FALSE(redHerringHasNonZeroDrawStats);
    EXPECT_EQ(redHerringLeaf.details->stepPreviewKind, FrameDebuggerStepPreviewKind::NotYetDrawn);

    // Real pivot leaf(0) + its one real entity child(1), red herring
    // parent(2) + its own child(3).
    EXPECT_EQ(snapshot.totalEventCount, 4);
}

// A graphics pass after a surviving compute pass is swept into "Other Render Passes".
TEST(FrameDebuggerSnapshotBuilderTest, GraphicsPassAfterSurvivingComputePassIsSweptIntoOtherRenderPassesGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    rg::RenderGraphPassSnapshot composite = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    composite.writeNames.push_back("GameViewComposited");
    composite.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(composite);

    rg::RenderGraphPassSnapshot pluginClear = MakePass("DemoRenderFeaturePlugin_Clear");
    pluginClear.renderPassEvent = rg::RenderPassEvent::AfterEverything;
    graphSnapshot.passesInExecutionOrder.push_back(pluginClear);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // "RenderOpaque" leaf + "Compute Dispatches (Post-GameView)" group +
    // the new "Other Render Passes" group - the actual fix.
    ASSERT_EQ(root.children.size(), 3u);
    EXPECT_EQ(root.children[0].name, "RenderOpaque");
    EXPECT_EQ(root.children[1].name, "Compute Dispatches (Post-GameView)");

    const FrameDebuggerEventNode& otherGroup = root.children[2];
    EXPECT_EQ(otherGroup.name, "Other Render Passes");
    ASSERT_EQ(otherGroup.children.size(), 1u);
    const FrameDebuggerEventNode& leaf = otherGroup.children[0];
    EXPECT_EQ(leaf.name, "DemoRenderFeaturePlugin_Clear");
    EXPECT_TRUE(leaf.isDrawCall);
    ASSERT_TRUE(leaf.details.has_value());
    EXPECT_EQ(leaf.details->passName, "DemoRenderFeaturePlugin_Clear");
    // Positioned strictly after the composite pass - PostComposite, the same
    // structural rule the Post-GameView compute loop already uses.
    EXPECT_EQ(leaf.details->stepPreviewKind, FrameDebuggerStepPreviewKind::PostComposite);
    ASSERT_EQ(leaf.children.size(), 1u);
    EXPECT_EQ(leaf.children[0].name, "Draw Mesh"); // Default RenderPassDrawKind::DrawMesh - never mistaken as compute.

    // RenderOpaque(0), composite parent(1) + child(2), pluginClear parent(3) + child(4).
    EXPECT_EQ(snapshot.totalEventCount, 5);
}

// A blit pass after the pivot is never taken for "RenderOpaque" and does not displace it.
TEST(FrameDebuggerSnapshotBuilderTest, BlitPassImmediatelyAfterPivotIsNeverTreatedAsRenderOpaqueAndDoesNotDisplaceRealRenderOpaque)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeBlitPass("HistoryBufferBlit"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("DrawSkyBackground"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // RenderOpaque leaf + HistoryBufferBlit leaf + DrawSkyBackground leaf -
    // no compute group of either flavor exists in this minimal fixture.
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[0];
    EXPECT_EQ(renderOpaqueLeaf.name, "RenderOpaque");
    // No DrawRecords() in this fixture's capture - genuinely zero children,
    // never fabricated ones borrowed from the Blit pass that follows it.
    EXPECT_TRUE(renderOpaqueLeaf.children.empty());

    const FrameDebuggerEventNode& blitLeaf = root.children[1];
    EXPECT_EQ(blitLeaf.name, "HistoryBufferBlit"); // Its own real name - never "RenderOpaque".
    EXPECT_TRUE(blitLeaf.isDrawCall);
    ASSERT_EQ(blitLeaf.children.size(), 1u); // Owns exactly one real child event, like every Graphics-family leaf.
    EXPECT_EQ(blitLeaf.children[0].name, "Blit"); // GraphicsChildEventLabelFor(RenderPassDrawKind::Blit).

    // A later "DrawSkyBackground" pass is still built as a normal graphics leaf.
    const FrameDebuggerEventNode& skyLeaf = root.children[2];
    EXPECT_EQ(skyLeaf.name, "DrawSkyBackground");
    ASSERT_EQ(skyLeaf.children.size(), 1u);
    EXPECT_EQ(skyLeaf.children[0].name, "Draw Mesh"); // Default RenderPassDrawKind::DrawMesh (fixture never set it).
}

// A blit pass swept into "Other Render Passes" gets a "Blit" child label.
TEST(FrameDebuggerSnapshotBuilderTest, BlitPassInOtherRenderPassesSweepGetsCorrectBlitLabel)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    rg::RenderGraphPassSnapshot composite = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    composite.writeNames.push_back("GameViewComposited");
    composite.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(composite);

    rg::RenderGraphPassSnapshot blitPass = MakeBlitPass("PostEverythingBlit");
    blitPass.renderPassEvent = rg::RenderPassEvent::AfterEverything;
    graphSnapshot.passesInExecutionOrder.push_back(blitPass);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // "RenderOpaque" leaf + "Compute Dispatches (Post-GameView)" group + the
    // "Other Render Passes" group holding the swept-up Blit pass.
    ASSERT_EQ(root.children.size(), 3u);
    EXPECT_EQ(root.children[0].name, "RenderOpaque");
    EXPECT_EQ(root.children[1].name, "Compute Dispatches (Post-GameView)");

    const FrameDebuggerEventNode& otherGroup = root.children[2];
    EXPECT_EQ(otherGroup.name, "Other Render Passes");
    ASSERT_EQ(otherGroup.children.size(), 1u);
    const FrameDebuggerEventNode& leaf = otherGroup.children[0];
    EXPECT_EQ(leaf.name, "PostEverythingBlit");
    EXPECT_TRUE(leaf.isDrawCall);
    ASSERT_EQ(leaf.children.size(), 1u);
    EXPECT_EQ(leaf.children[0].name, "Blit"); // GraphicsChildEventLabelFor(RenderPassDrawKind::Blit) - never a mislabel.
}

// A Debug-category feature pass inside the view region is a normal visible leaf.
TEST(FrameDebuggerSnapshotBuilderTest, DebugCategoryGraphicsPassInsideViewRegionIsNowVisibleAsANormalLeaf)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass);
    graphSnapshot.passesInExecutionOrder.push_back(
        MakeGraphicsPassWithCategory("SomeDebugFlavoredFeaturePass", rg::RenderPassCategory::Debug));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // Direct siblings under root: the ordinary view-region walk finds the Debug-category pass.
    ASSERT_EQ(root.children.size(), 3u);
    EXPECT_EQ(root.children[0].name, "RenderOpaque");
    EXPECT_EQ(root.children[1].name, "DrawSkyBackground");
    const FrameDebuggerEventNode& debugLeaf = root.children[2];
    EXPECT_EQ(debugLeaf.name, "SomeDebugFlavoredFeaturePass");
    EXPECT_TRUE(debugLeaf.isDrawCall);
    ASSERT_TRUE(debugLeaf.details.has_value());
    EXPECT_EQ(debugLeaf.details->passName, "SomeDebugFlavoredFeaturePass");
    ASSERT_EQ(debugLeaf.children.size(), 1u);
    EXPECT_EQ(debugLeaf.children[0].name, "Draw Mesh");
}

} // namespace
} // namespace gte
