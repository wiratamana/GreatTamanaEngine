// Unit tests for the Frame Debugger's real renderer-capture instrumentation
// (src/Editor/FrameDebuggerCapture.h) - FrameDebuggerCaptureContext::
// RecordDraw()/Reset() and DescribeStandardPipelineState() are both plain,
// ImGui-free, no-live-VkDevice-needed logic, so this is directly
// Tier-1-testable exactly like FrameDebuggerData.h despite living under
// src/Editor/ - see AGENTS.md, "Testability & Regression Safety". Only
// built when GTE_ENABLE_EDITOR is ON, since FrameDebuggerCapture.h/.cpp are
// only compiled into gte_core then (see the root CMakeLists.txt's "Editor
// Module Structure") - the same "zero-touch when off" rule already applied
// to tests/Editor/FrameDebuggerDataTests.cpp.
//
// task_manager/frame-debugger-3 campaign, PHASE1
// (PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md).

#include "Editor/FrameDebuggerCapture.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(FrameDebuggerCaptureContextTest, StartsEmpty)
{
    FrameDebuggerCaptureContext capture;
    EXPECT_TRUE(capture.PipelineDebugNames().empty());
    EXPECT_TRUE(capture.MaterialTextureDebugNames().empty());
    EXPECT_EQ(capture.DrawCallCount(), 0);
    EXPECT_TRUE(ApproximatelyEqual(capture.LastViewProjection(), Mat4::Identity()));
}

TEST(FrameDebuggerCaptureContextTest, RecordDrawDeduplicatesRepeatedNames)
{
    FrameDebuggerCaptureContext capture;
    const Mat4 viewProj = Mat4::Translation(Vec3(1.0f, 2.0f, 3.0f));

    capture.RecordDraw("Mesh.vert/Mesh.frag (PositionNormal)", "MaterialTexture abc123", viewProj);
    capture.RecordDraw("Mesh.vert/Mesh.frag (PositionNormal)", "MaterialTexture abc123", viewProj);

    ASSERT_EQ(capture.PipelineDebugNames().size(), 1u);
    EXPECT_EQ(capture.PipelineDebugNames()[0], "Mesh.vert/Mesh.frag (PositionNormal)");
    ASSERT_EQ(capture.MaterialTextureDebugNames().size(), 1u);
    EXPECT_EQ(capture.MaterialTextureDebugNames()[0], "MaterialTexture abc123");
    EXPECT_EQ(capture.DrawCallCount(), 2); // Every RecordDraw() call counts, even a name-duplicate one.
}

TEST(FrameDebuggerCaptureContextTest, RecordDrawKeepsDistinctNamesSeparate)
{
    FrameDebuggerCaptureContext capture;
    const Mat4 viewProj = Mat4::Identity();

    capture.RecordDraw("Mesh.vert/Mesh.frag (PositionNormal)", "MaterialTexture abc123", viewProj);
    capture.RecordDraw("TexturedMesh.vert/TexturedMesh.frag (PositionNormalUv)", "MaterialTexture def456", viewProj);

    ASSERT_EQ(capture.PipelineDebugNames().size(), 2u);
    EXPECT_EQ(capture.PipelineDebugNames()[0], "Mesh.vert/Mesh.frag (PositionNormal)");
    EXPECT_EQ(capture.PipelineDebugNames()[1], "TexturedMesh.vert/TexturedMesh.frag (PositionNormalUv)");
    ASSERT_EQ(capture.MaterialTextureDebugNames().size(), 2u);
    EXPECT_EQ(capture.MaterialTextureDebugNames()[0], "MaterialTexture abc123");
    EXPECT_EQ(capture.MaterialTextureDebugNames()[1], "MaterialTexture def456");
    EXPECT_EQ(capture.DrawCallCount(), 2);
}

TEST(FrameDebuggerCaptureContextTest, RecordDrawIgnoresEmptyMaterialTextureName)
{
    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("Triangle.vert/Triangle.frag (PositionColor)", std::string(), Mat4::Identity());

    EXPECT_EQ(capture.PipelineDebugNames().size(), 1u);
    EXPECT_TRUE(capture.MaterialTextureDebugNames().empty()); // An untextured draw never adds a texture name.
    EXPECT_EQ(capture.DrawCallCount(), 1);
}

TEST(FrameDebuggerCaptureContextTest, RecordDrawRemembersLastViewProjection)
{
    FrameDebuggerCaptureContext capture;
    const Mat4 first = Mat4::Translation(Vec3(1.0f, 0.0f, 0.0f));
    const Mat4 second = Mat4::Translation(Vec3(0.0f, 5.0f, 0.0f));

    capture.RecordDraw("PipelineA", "", first);
    capture.RecordDraw("PipelineB", "", second);

    EXPECT_TRUE(ApproximatelyEqual(capture.LastViewProjection(), second));
}

TEST(FrameDebuggerCaptureContextTest, ResetClearsEverythingBackToEmpty)
{
    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("PipelineA", "TextureA", Mat4::Translation(Vec3(1.0f, 1.0f, 1.0f)));
    capture.Reset();

    EXPECT_TRUE(capture.PipelineDebugNames().empty());
    EXPECT_TRUE(capture.MaterialTextureDebugNames().empty());
    EXPECT_EQ(capture.DrawCallCount(), 0);
    EXPECT_TRUE(ApproximatelyEqual(capture.LastViewProjection(), Mat4::Identity()));
}

TEST(DescribeStandardPipelineStateTest, ReportsRealHardcodedPipelineCppValues)
{
    // Cross-checked directly against Pipeline.cpp's own real
    // VkPipelineColorBlendAttachmentState/VkPipelineDepthStencilStateCreateInfo
    // construction at the time this test was written - if a future change
    // to Pipeline.cpp's hardcoded state ever silently drifts, THIS test (not
    // just DescribeStandardPipelineState() itself) is the regression-safety
    // net that catches it (see this phase's own Step 3.5).
    const FrameDebuggerStandardPipelineState state = DescribeStandardPipelineState();

    EXPECT_EQ(state.blendMode, "Opaque (no blend)"); // colorBlendAttachment.blendEnable = VK_FALSE.
    EXPECT_EQ(state.zClip, "On");
    EXPECT_EQ(state.zTest, "Less"); // depthCompareOp = VK_COMPARE_OP_LESS, not LEqual/LessEqual.
    EXPECT_EQ(state.zWrite, "On"); // depthWriteEnable = VK_TRUE.
    EXPECT_EQ(state.cull, "None"); // rasterizer.cullMode = VK_CULL_MODE_NONE.
    EXPECT_EQ(state.stencilRef, "n/a (no stencil test)");
    EXPECT_EQ(state.stencilComp, "n/a (no stencil test)");
    EXPECT_EQ(state.stencilPass, "n/a (no stencil test)");
    EXPECT_EQ(state.stencilFail, "n/a (no stencil test)");
    EXPECT_EQ(state.stencilZFail, "n/a (no stencil test)");

    // Every field must be non-empty - see this phase's own Step 3.5.
    EXPECT_FALSE(state.blendMode.empty());
    EXPECT_FALSE(state.zClip.empty());
    EXPECT_FALSE(state.zTest.empty());
    EXPECT_FALSE(state.zWrite.empty());
    EXPECT_FALSE(state.cull.empty());
    EXPECT_FALSE(state.stencilRef.empty());
    EXPECT_FALSE(state.stencilComp.empty());
    EXPECT_FALSE(state.stencilPass.empty());
    EXPECT_FALSE(state.stencilFail.empty());
    EXPECT_FALSE(state.stencilZFail.empty());
}

} // namespace
} // namespace gte
