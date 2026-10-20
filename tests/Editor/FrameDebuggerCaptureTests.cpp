// Tier-1 tests of FrameDebuggerCaptureContext's recording and the standard pipeline state descriptions.
// Built only when GTE_ENABLE_EDITOR is ON.

#include "Editor/FrameDebuggerCapture.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte {
namespace {

using Slots = std::vector<FrameDebuggerMaterialSlot>;

Slots FourSlots(const std::string& baseColor, const std::string& normal = {}, const std::string& metallicRoughness = {},
    const std::string& occlusion = {})
{
    return { { "Base Color", baseColor }, { "Normal", normal }, { "Metallic-Roughness", metallicRoughness },
        { "Occlusion", occlusion } };
}

TEST(FrameDebuggerCaptureContextTest, StartsEmpty)
{
    FrameDebuggerCaptureContext capture;
    EXPECT_TRUE(capture.PipelineDebugNames().empty());
    EXPECT_TRUE(capture.MaterialSlotRows().empty());
    EXPECT_EQ(capture.DrawCallCount(), 0);
    EXPECT_TRUE(ApproximatelyEqual(capture.LastViewProjection(), Mat4::Identity()));
    EXPECT_TRUE(capture.DrawRecords().empty());
}

TEST(FrameDebuggerCaptureContextTest, RecordDrawDeduplicatesRepeatedNames)
{
    FrameDebuggerCaptureContext capture;
    const Mat4 viewProj = Mat4::Translation(Vec3(1.0f, 2.0f, 3.0f));
    const Slots slots = FourSlots("MaterialTexture abc123");

    capture.RecordDraw("Mesh.vert/Mesh.frag (PositionNormal)", slots, viewProj);
    capture.RecordDraw("Mesh.vert/Mesh.frag (PositionNormal)", slots, viewProj);

    ASSERT_EQ(capture.PipelineDebugNames().size(), 1u);
    EXPECT_EQ(capture.PipelineDebugNames()[0], "Mesh.vert/Mesh.frag (PositionNormal)");
    ASSERT_EQ(capture.MaterialSlotRows().size(), 1u);
    EXPECT_EQ(capture.MaterialSlotRows()[0], (FrameDebuggerMaterialSlot { "Base Color", "MaterialTexture abc123" }));
    EXPECT_EQ(capture.DrawCallCount(), 2);
}

TEST(FrameDebuggerCaptureContextTest, RecordDrawKeepsDistinctNamesSeparate)
{
    FrameDebuggerCaptureContext capture;
    const Mat4 viewProj = Mat4::Identity();

    capture.RecordDraw("Mesh.vert/Mesh.frag (PositionNormal)", FourSlots("MaterialTexture abc123"), viewProj);
    capture.RecordDraw("TexturedMesh.vert/TexturedMesh.frag (PositionNormalUv)", FourSlots("MaterialTexture def456"), viewProj);

    ASSERT_EQ(capture.PipelineDebugNames().size(), 2u);
    ASSERT_EQ(capture.MaterialSlotRows().size(), 2u);
    EXPECT_EQ(capture.MaterialSlotRows()[0].imageName, "MaterialTexture abc123");
    EXPECT_EQ(capture.MaterialSlotRows()[1].imageName, "MaterialTexture def456");
    EXPECT_EQ(capture.DrawCallCount(), 2);
}

TEST(FrameDebuggerCaptureContextTest, RecordDrawSkipsEmptyImageNames)
{
    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("Triangle.vert/Triangle.frag (PositionColor)", Slots {}, Mat4::Identity());
    capture.RecordDraw("PipelineA", FourSlots(""), Mat4::Identity());

    EXPECT_EQ(capture.PipelineDebugNames().size(), 2u);
    EXPECT_TRUE(capture.MaterialSlotRows().empty());
    EXPECT_EQ(capture.DrawCallCount(), 2);
}

TEST(FrameDebuggerCaptureContextTest, MaterialSlotRowsDedupeBySlotAndImageInFirstSeenOrder)
{
    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("P", FourSlots("Base A", "Normal A", "MR A", "Occ A"), Mat4::Identity());
    capture.RecordDraw("P", FourSlots("Base B", "Normal A", "MR A", "Occ A"), Mat4::Identity());

    const auto& rows = capture.MaterialSlotRows();
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_EQ(rows[0], (FrameDebuggerMaterialSlot { "Base Color", "Base A" }));
    EXPECT_EQ(rows[1], (FrameDebuggerMaterialSlot { "Normal", "Normal A" }));
    EXPECT_EQ(rows[2], (FrameDebuggerMaterialSlot { "Metallic-Roughness", "MR A" }));
    EXPECT_EQ(rows[3], (FrameDebuggerMaterialSlot { "Occlusion", "Occ A" }));
    EXPECT_EQ(rows[4], (FrameDebuggerMaterialSlot { "Base Color", "Base B" }));
}

TEST(FrameDebuggerCaptureContextTest, RecordDrawRemembersLastViewProjection)
{
    FrameDebuggerCaptureContext capture;
    const Mat4 first = Mat4::Translation(Vec3(1.0f, 0.0f, 0.0f));
    const Mat4 second = Mat4::Translation(Vec3(0.0f, 5.0f, 0.0f));

    capture.RecordDraw("PipelineA", Slots {}, first);
    capture.RecordDraw("PipelineB", Slots {}, second);

    EXPECT_TRUE(ApproximatelyEqual(capture.LastViewProjection(), second));
}

TEST(FrameDebuggerCaptureContextTest, ResetClearsEverythingBackToEmpty)
{
    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("PipelineA", FourSlots("TextureA"), Mat4::Translation(Vec3(1.0f, 1.0f, 1.0f)));
    capture.RecordEntityDraw(2, 1, "terrain", "PipelineA", FourSlots("TextureA"), 1045458);
    capture.Reset();

    EXPECT_TRUE(capture.PipelineDebugNames().empty());
    EXPECT_TRUE(capture.MaterialSlotRows().empty());
    EXPECT_EQ(capture.DrawCallCount(), 0);
    EXPECT_TRUE(ApproximatelyEqual(capture.LastViewProjection(), Mat4::Identity()));
    EXPECT_TRUE(capture.DrawRecords().empty());
}

TEST(FrameDebuggerCaptureContextTest, RecordEntityDrawAppendsOneRecordPerCall)
{
    FrameDebuggerCaptureContext capture;

    capture.RecordEntityDraw(
        2, 1, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", FourSlots("MaterialTexture abc123"), 1045458);
    capture.RecordEntityDraw(3, 1, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", Slots {}, 12);

    ASSERT_EQ(capture.DrawRecords().size(), 2u);

    const FrameDebuggerDrawRecord& first = capture.DrawRecords()[0];
    EXPECT_EQ(first.entityIndex, 2u);
    EXPECT_EQ(first.entityGeneration, 1u);
    EXPECT_EQ(first.displayName, "terrain");
    EXPECT_EQ(first.pipelineDebugName, "Mesh.vert/Mesh.frag (PositionNormal)");
    ASSERT_EQ(first.materialSlots.size(), 4u);
    EXPECT_EQ(first.materialSlots[0].imageName, "MaterialTexture abc123");
    EXPECT_EQ(first.triangleCount, 1045458u);

    const FrameDebuggerDrawRecord& second = capture.DrawRecords()[1];
    EXPECT_EQ(second.entityIndex, 3u);
    EXPECT_EQ(second.displayName, "SmokeTestCube");
    EXPECT_TRUE(second.materialSlots.empty()); // Untextured draw.
    EXPECT_EQ(second.triangleCount, 12u);
}

TEST(FrameDebuggerCaptureContextTest, RecordEntityDrawNeverDeduplicates)
{
    FrameDebuggerCaptureContext capture;

    capture.RecordEntityDraw(5, 2, "terrain", "PipelineA", FourSlots("TextureA"), 100);
    capture.RecordEntityDraw(5, 2, "terrain", "PipelineA", FourSlots("TextureA"), 100);

    ASSERT_EQ(capture.DrawRecords().size(), 2u);
}

TEST(FrameDebuggerCaptureContextTest, RecordEntityDrawDoesNotAffectRecordDrawBookkeeping)
{
    FrameDebuggerCaptureContext capture;

    capture.RecordEntityDraw(1, 1, "terrain", "PipelineA", FourSlots("TextureA"), 100);

    EXPECT_TRUE(capture.PipelineDebugNames().empty());
    EXPECT_TRUE(capture.MaterialSlotRows().empty());
    EXPECT_EQ(capture.DrawCallCount(), 0);
    ASSERT_EQ(capture.DrawRecords().size(), 1u);
}

TEST(DescribeStandardPipelineStateTest, ReportsRealHardcodedPipelineCppValues)
{
    // Must match the fixed state in Pipeline.cpp.
    const FrameDebuggerStandardPipelineState state = DescribeStandardPipelineState();

    EXPECT_EQ(state.blendMode, "Opaque (no blend)");
    EXPECT_EQ(state.zClip, "On");
    EXPECT_EQ(state.zTest, "Less");
    EXPECT_EQ(state.zWrite, "On");
    EXPECT_EQ(state.cull, "None");
    EXPECT_EQ(state.stencilRef, "n/a (no stencil test)");
    EXPECT_EQ(state.stencilComp, "n/a (no stencil test)");
    EXPECT_EQ(state.stencilPass, "n/a (no stencil test)");
    EXPECT_EQ(state.stencilFail, "n/a (no stencil test)");
    EXPECT_EQ(state.stencilZFail, "n/a (no stencil test)");
}

TEST(DescribeSkyBackgroundPipelineStateTest, ReturnsRealDistinctValues)
{
    // The sky state differs from the standard one only in depth test and depth write.
    const FrameDebuggerStandardPipelineState sky = DescribeSkyBackgroundPipelineState();
    const FrameDebuggerStandardPipelineState standard = DescribeStandardPipelineState();

    EXPECT_EQ(sky.zTest, "Equal");
    EXPECT_EQ(sky.zWrite, "Off");

    EXPECT_EQ(sky.blendMode, standard.blendMode);
    EXPECT_EQ(sky.zClip, standard.zClip);
    EXPECT_EQ(sky.cull, standard.cull);
    EXPECT_EQ(sky.stencilRef, standard.stencilRef);
    EXPECT_EQ(sky.stencilComp, standard.stencilComp);
    EXPECT_EQ(sky.stencilPass, standard.stencilPass);
    EXPECT_EQ(sky.stencilFail, standard.stencilFail);
    EXPECT_EQ(sky.stencilZFail, standard.stencilZFail);
}

} // namespace
} // namespace gte
