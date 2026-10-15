// Tier-1 tests for the pure parts of rg::CommandBuffer (push-size check, construction route).

#include "Renderer/RenderGraph/CommandBuffer.h"

#include <gtest/gtest.h>

#include <type_traits>

namespace gte::rg {
namespace {

// Only PassContext and AdoptRaw() can build a CommandBuffer.
static_assert(!std::is_default_constructible_v<CommandBuffer>);
static_assert(!std::is_constructible_v<CommandBuffer, VkCommandBuffer, Renderer*, DrawStats*,
    CommandBuffer::WriteTargetInfo, FrameDebuggerEventSink*>);

TEST(PushConstantSizeMatchesTests, ReflectedSizeZeroAlwaysMatchesRegardlessOfSuppliedSize)
{
    // Size 0 means "no reflected block to check against".
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
    EXPECT_FALSE(PushConstantSizeMatches(4, 8));
    EXPECT_FALSE(PushConstantSizeMatches(16, 8));
    EXPECT_FALSE(PushConstantSizeMatches(0, 8));
}

} // namespace
} // namespace gte::rg
