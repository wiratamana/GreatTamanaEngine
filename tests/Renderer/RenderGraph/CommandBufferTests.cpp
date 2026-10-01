// task_manager/better-render-pass-1 campaign, PHASE3
// (PHASE3_ENGINE_COMMAND_BUFFER_AND_TYPE_SAFE_PUSH_CONSTANTS.md) - Tier-1
// tests for gte::rg::PushConstantSizeMatches() (src/Renderer/RenderGraph/
// CommandBuffer.h), the pure logic behind CommandBuffer::SetPushConstants<T>()'s
// debug-only size-mismatch assertion (R4). Everything else CommandBuffer does
// (BindComputePipeline()/Dispatch()/DispatchOverSize()/Draw()/etc.) needs a
// live VkCommandBuffer/Renderer/VkDevice and is only verified live (Tier 2)
// - see PHASE3_COMPLETION_REPORT.md's own "Manual/Tier-2 smoke test" section.

#include "Renderer/RenderGraph/CommandBuffer.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(PushConstantSizeMatchesTests, ReflectedSizeZeroAlwaysMatchesRegardlessOfSuppliedSize)
{
    // A manually-built ComputePipeline (ComputePipeline.h's MANUAL path) has
    // no reflected push-constant metadata at all - PushConstantSize() == 0
    // means "nothing to check against", never a real zero-byte push-constant
    // block - so this must report a match for any supplied size at all.
    EXPECT_TRUE(PushConstantSizeMatches(0, 0));
    EXPECT_TRUE(PushConstantSizeMatches(16, 0));
    EXPECT_TRUE(PushConstantSizeMatches(128, 0));
}

TEST(PushConstantSizeMatchesTests, MatchingSuppliedAndReflectedSizeReportsTrue)
{
    EXPECT_TRUE(PushConstantSizeMatches(8, 8));
    EXPECT_TRUE(PushConstantSizeMatches(64, 64));
}

TEST(PushConstantSizeMatchesTests, MismatchedSuppliedAndReflectedSizeReportsFalse)
{
    // This is the real bug class R4 exists to catch: a hand-restated
    // push-constant struct size silently drifting out of sync with the
    // shader's own reflected layout(push_constant) block.
    EXPECT_FALSE(PushConstantSizeMatches(4, 8));
    EXPECT_FALSE(PushConstantSizeMatches(16, 8));
    EXPECT_FALSE(PushConstantSizeMatches(0, 8));
}

} // namespace
} // namespace gte::rg
