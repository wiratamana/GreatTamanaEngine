// Unit tests for the editor-core-separation-22 campaign's PHASE3
// (task_manager/editor-core-separation-22/
// PHASE3_FIX_AUDIT_FINDINGS_SIDE_CHANNEL_LEAKS.md) fix for
// PHASE2_COMPLETION_REPORT.md's own finding #19 - the ONLY Confirmed-Lie row
// in that phase's ledger: an ENABLED batch's own entities used to be
// unconditionally added to RenderOpaque's own exclusion set at collection
// time, before that specific batch's own "<batch> IndirectDraw" pass toggle
// state was even known - so disabling ONE dynamically-named per-batch pass
// made its own entities silently, completely invisible (excluded from the
// normal draw path AND undrawn by the now-disabled indirect path).
// Entirely Tier-1 - no Vulkan device, no live RenderPipeline/RenderGraphBuilder/
// RenderPassToggleRegistry involved at all, mirroring
// AtmospherePassToggleLogicTests.cpp's own precedent (Core/
// GpuDrivenBatchEntityExclusionLogic.h deliberately takes the resolved bool
// as a plain parameter for exactly this reason).

#include "Core/GpuDrivenBatchEntityExclusionLogic.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

Entity MakeEntity(std::uint32_t index)
{
    Entity e;
    e.index = index;
    e.generation = 1;
    return e;
}

TEST(GpuDrivenBatchEntityExclusionLogicTest, EnabledBatchEntitiesAreAddedToExclusionSet)
{
    const std::vector<Entity> entities = { MakeEntity(1), MakeEntity(2), MakeEntity(3) };
    std::unordered_set<Entity> excluded;

    AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(/*indirectDrawPassEnabledThisFrame=*/true, entities, excluded);

    EXPECT_EQ(excluded.size(), 3u);
    EXPECT_TRUE(excluded.count(MakeEntity(1)));
    EXPECT_TRUE(excluded.count(MakeEntity(2)));
    EXPECT_TRUE(excluded.count(MakeEntity(3)));
}

TEST(GpuDrivenBatchEntityExclusionLogicTest, DisabledBatchEntitiesAreNeverAddedToExclusionSet)
{
    // This is the exact fix for finding #19: a disabled "<batch> IndirectDraw"
    // pass must leave its own entities OUT of the exclusion set entirely, so
    // RenderOpaque's own normal per-entity draw path (which only skips
    // entities actually present in this set) picks them up instead - the
    // honest fallback, instead of the entity vanishing from both paths.
    const std::vector<Entity> entities = { MakeEntity(1), MakeEntity(2) };
    std::unordered_set<Entity> excluded;

    AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(
        /*indirectDrawPassEnabledThisFrame=*/false, entities, excluded);

    EXPECT_TRUE(excluded.empty());
}

TEST(GpuDrivenBatchEntityExclusionLogicTest, MultipleBatchesAccumulateIntoTheSameSet)
{
    // Mirrors the real call site's own shape (Core.cpp's "GpuDrivenBatches"
    // provider): called once per batch, into the SAME m_gpuDrivenBatchedEntitiesThisFrame
    // set - an enabled batch's entities must not be lost/overwritten by a
    // later, disabled batch's own (empty) contribution.
    std::unordered_set<Entity> excluded;

    AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(
        /*indirectDrawPassEnabledThisFrame=*/true, { MakeEntity(10) }, excluded);
    AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(
        /*indirectDrawPassEnabledThisFrame=*/false, { MakeEntity(20) }, excluded);
    AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(
        /*indirectDrawPassEnabledThisFrame=*/true, { MakeEntity(30) }, excluded);

    EXPECT_EQ(excluded.size(), 2u);
    EXPECT_TRUE(excluded.count(MakeEntity(10)));
    EXPECT_FALSE(excluded.count(MakeEntity(20)));
    EXPECT_TRUE(excluded.count(MakeEntity(30)));
}

TEST(GpuDrivenBatchEntityExclusionLogicTest, EmptyEntityListIsANoOpEitherWay)
{
    std::unordered_set<Entity> excluded;

    AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(/*indirectDrawPassEnabledThisFrame=*/true, {}, excluded);
    AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(/*indirectDrawPassEnabledThisFrame=*/false, {}, excluded);

    EXPECT_TRUE(excluded.empty());
}

} // namespace
} // namespace gte
