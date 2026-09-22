// Unit tests for RenderSystem's pure batching/grouping logic
// (src/Game/RenderBatching.h/.cpp) - GPU-Driven Frustum Culling + Indirect
// Draw campaign (render-pass-5), PHASE4 (task_manager/render-pass-5/
// PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md). Pure logic, no
// Renderer/live-GPU dependency at all - fed entirely with hand-built
// DrawCommand values, same "Tier 1" convention as
// tests/Game/RenderSystemTests.cpp.

#include "Game/RenderBatching.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

DrawCommand MakeCommand(std::uint32_t entityIndex, std::uint32_t meshIndex, std::uint32_t pipelineIndex)
{
    DrawCommand command;
    command.entity = Entity{ entityIndex, 1 };
    command.mesh = MeshHandle{ meshIndex, 1 };
    command.pipeline = PipelineHandle{ pipelineIndex, 1 };
    return command;
}

TEST(RenderBatchingTest, GroupingEmptyInputProducesNoGroups)
{
    const std::vector<DrawCommand> commands;

    const std::vector<RenderBatchGroup> groups = GroupDrawCommandsByMeshAndPipeline(commands);

    EXPECT_TRUE(groups.empty());
}

TEST(RenderBatchingTest, GroupingAllSameMeshAndPipelineProducesOneGroup)
{
    std::vector<DrawCommand> commands;
    for (std::uint32_t i = 0; i < 5; ++i) {
        commands.push_back(MakeCommand(i, /*meshIndex=*/1, /*pipelineIndex=*/1));
    }

    const std::vector<RenderBatchGroup> groups = GroupDrawCommandsByMeshAndPipeline(commands);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].mesh, (MeshHandle{ 1, 1 }));
    EXPECT_EQ(groups[0].pipeline, (PipelineHandle{ 1, 1 }));
    ASSERT_EQ(groups[0].commands.size(), 5u);
    for (std::uint32_t i = 0; i < 5; ++i) {
        EXPECT_EQ(groups[0].commands[i].entity, (Entity{ i, 1 }));
    }
}

// Several interleaved groups - proves grouping is by (mesh, pipeline)
// IDENTITY, not merely by adjacency in the input, and that each group's own
// FIRST-SEEN order (both across groups AND within one group) is preserved
// deterministically - a real testability requirement (PHASE4's own doc).
TEST(RenderBatchingTest, InterleavedCommandsGroupByMeshAndPipelineAndPreserveOrder)
{
    std::vector<DrawCommand> commands;
    commands.push_back(MakeCommand(0, /*meshIndex=*/1, /*pipelineIndex=*/1)); // Group A, first-seen.
    commands.push_back(MakeCommand(1, /*meshIndex=*/2, /*pipelineIndex=*/1)); // Group B, first-seen.
    commands.push_back(MakeCommand(2, /*meshIndex=*/1, /*pipelineIndex=*/1)); // Group A again.
    commands.push_back(MakeCommand(3, /*meshIndex=*/1, /*pipelineIndex=*/2)); // Group C (same mesh, different pipeline).
    commands.push_back(MakeCommand(4, /*meshIndex=*/2, /*pipelineIndex=*/1)); // Group B again.
    commands.push_back(MakeCommand(5, /*meshIndex=*/1, /*pipelineIndex=*/1)); // Group A again.

    const std::vector<RenderBatchGroup> groups = GroupDrawCommandsByMeshAndPipeline(commands);

    ASSERT_EQ(groups.size(), 3u);

    // Group order == first-seen order: A, B, C.
    EXPECT_EQ(groups[0].mesh, (MeshHandle{ 1, 1 }));
    EXPECT_EQ(groups[0].pipeline, (PipelineHandle{ 1, 1 }));
    ASSERT_EQ(groups[0].commands.size(), 3u);
    EXPECT_EQ(groups[0].commands[0].entity, (Entity{ 0, 1 }));
    EXPECT_EQ(groups[0].commands[1].entity, (Entity{ 2, 1 }));
    EXPECT_EQ(groups[0].commands[2].entity, (Entity{ 5, 1 }));

    EXPECT_EQ(groups[1].mesh, (MeshHandle{ 2, 1 }));
    EXPECT_EQ(groups[1].pipeline, (PipelineHandle{ 1, 1 }));
    ASSERT_EQ(groups[1].commands.size(), 2u);
    EXPECT_EQ(groups[1].commands[0].entity, (Entity{ 1, 1 }));
    EXPECT_EQ(groups[1].commands[1].entity, (Entity{ 4, 1 }));

    EXPECT_EQ(groups[2].mesh, (MeshHandle{ 1, 1 }));
    EXPECT_EQ(groups[2].pipeline, (PipelineHandle{ 2, 1 }));
    ASSERT_EQ(groups[2].commands.size(), 1u);
    EXPECT_EQ(groups[2].commands[0].entity, (Entity{ 3, 1 }));
}

// Same MeshHandle::index but a DIFFERENT generation must be treated as a
// genuinely different mesh identity (mirrors ResourcePool's own generation-
// guard semantics) - a real regression case, not merely a boundary nicety.
TEST(RenderBatchingTest, DifferentGenerationIsADifferentGroup)
{
    std::vector<DrawCommand> commands;
    DrawCommand a = MakeCommand(0, 1, 1);
    DrawCommand b = MakeCommand(1, 1, 1);
    b.mesh.generation = 2; // Same index, different generation.
    commands.push_back(a);
    commands.push_back(b);

    const std::vector<RenderBatchGroup> groups = GroupDrawCommandsByMeshAndPipeline(commands);

    ASSERT_EQ(groups.size(), 2u);
}

RenderBatchGroup MakeGroupWithNCommands(std::size_t n)
{
    RenderBatchGroup group;
    group.mesh = MeshHandle{ 1, 1 };
    group.pipeline = PipelineHandle{ 1, 1 };
    for (std::size_t i = 0; i < n; ++i) {
        group.commands.push_back(MakeCommand(static_cast<std::uint32_t>(i), 1, 1));
    }
    return group;
}

// IsGpuDrivenEligible() - Locked Design Decision 7, PHASE0. Every
// true/false combination of its 4 boolean-ish inputs against a fixed
// threshold, plus the exact boundary at the threshold itself.

TEST(RenderBatchingTest, EligibleWhenAllFourConditionsHold)
{
    const RenderBatchGroup group = MakeGroupWithNCommands(4);
    EXPECT_TRUE(IsGpuDrivenEligible(group, /*hasIndexBuffer=*/true, VertexLayout::PositionNormal,
        /*isGpuSkinned=*/false, /*minInstancesForGpuDrivenBatch=*/4));
}

TEST(RenderBatchingTest, IneligibleBelowInstanceThreshold)
{
    const RenderBatchGroup group = MakeGroupWithNCommands(3); // One below the threshold of 4.
    EXPECT_FALSE(IsGpuDrivenEligible(group, /*hasIndexBuffer=*/true, VertexLayout::PositionNormal,
        /*isGpuSkinned=*/false, /*minInstancesForGpuDrivenBatch=*/4));
}

TEST(RenderBatchingTest, EligibleExactlyAtInstanceThresholdBoundary)
{
    const RenderBatchGroup group = MakeGroupWithNCommands(4); // Exactly at the threshold.
    EXPECT_TRUE(IsGpuDrivenEligible(group, /*hasIndexBuffer=*/true, VertexLayout::PositionNormal,
        /*isGpuSkinned=*/false, /*minInstancesForGpuDrivenBatch=*/4));
}

TEST(RenderBatchingTest, IneligibleWithoutIndexBuffer)
{
    const RenderBatchGroup group = MakeGroupWithNCommands(4);
    EXPECT_FALSE(IsGpuDrivenEligible(group, /*hasIndexBuffer=*/false, VertexLayout::PositionNormal,
        /*isGpuSkinned=*/false, /*minInstancesForGpuDrivenBatch=*/4));
}

TEST(RenderBatchingTest, IneligibleForPositionColorVertexLayout)
{
    const RenderBatchGroup group = MakeGroupWithNCommands(4);
    EXPECT_FALSE(IsGpuDrivenEligible(group, /*hasIndexBuffer=*/true, VertexLayout::PositionColor,
        /*isGpuSkinned=*/false, /*minInstancesForGpuDrivenBatch=*/4));
}

TEST(RenderBatchingTest, IneligibleForPositionNormalUvVertexLayout)
{
    const RenderBatchGroup group = MakeGroupWithNCommands(4);
    EXPECT_FALSE(IsGpuDrivenEligible(group, /*hasIndexBuffer=*/true, VertexLayout::PositionNormalUv,
        /*isGpuSkinned=*/false, /*minInstancesForGpuDrivenBatch=*/4));
}

TEST(RenderBatchingTest, IneligibleForPositionNormalInstancedVertexLayout)
{
    // A group whose Pipeline was ALREADY built as PositionNormalInstanced
    // should never be reported eligible again by this pure decision - PHASE0's
    // Locked Design Decision 7(c) requires EXACTLY PositionNormal (the
    // original, non-instanced pipeline); PHASE4/5's own resolution step is
    // what maps such a group onto the instanced variant, never the other way
    // around.
    const RenderBatchGroup group = MakeGroupWithNCommands(4);
    EXPECT_FALSE(IsGpuDrivenEligible(group, /*hasIndexBuffer=*/true, VertexLayout::PositionNormalInstanced,
        /*isGpuSkinned=*/false, /*minInstancesForGpuDrivenBatch=*/4));
}

TEST(RenderBatchingTest, IneligibleWhenGpuSkinned)
{
    const RenderBatchGroup group = MakeGroupWithNCommands(4);
    EXPECT_FALSE(IsGpuDrivenEligible(group, /*hasIndexBuffer=*/true, VertexLayout::PositionNormal,
        /*isGpuSkinned=*/true, /*minInstancesForGpuDrivenBatch=*/4));
}

TEST(RenderBatchingTest, IneligibleWhenMultipleConditionsFailSimultaneously)
{
    const RenderBatchGroup group = MakeGroupWithNCommands(1); // Below threshold AND...
    EXPECT_FALSE(IsGpuDrivenEligible(group, /*hasIndexBuffer=*/false, VertexLayout::PositionColor,
        /*isGpuSkinned=*/true, /*minInstancesForGpuDrivenBatch=*/4)); // ...no index buffer, wrong layout, GPU-skinned.
}

TEST(RenderBatchingTest, KMinInstancesForGpuDrivenBatchDefaultsToFour)
{
    EXPECT_EQ(kMinInstancesForGpuDrivenBatch, 4u);
}

} // namespace
} // namespace gte
