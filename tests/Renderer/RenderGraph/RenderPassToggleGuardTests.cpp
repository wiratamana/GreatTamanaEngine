// Unit tests for the editor-core-separation-22 campaign's PHASE1
// (task_manager/editor-core-separation-22/
// PHASE1_FIX_DRAWSKYBACKGROUND_TOGGLE_SIDE_CHANNEL_LEAK.md) new, generic,
// early toggle guard (src/Renderer/RenderGraph/RenderPassToggleGuard.h).
// Entirely Tier-1 - a real RenderPassToggleRegistry instance is involved
// (it is a plain, dependency-free, gte_core-tier class - see
// RenderPassToggleRegistryTests.cpp's own precedent), but no Vulkan device,
// no live RenderPipeline/RenderGraphBuilder involved at all, mirroring
// AtmospherePassToggleLogicTests.cpp's/
// RenderPassToggleChangeDetectionLogicTests.cpp's own precedent.

#include "Renderer/RenderGraph/RenderPassToggleGuard.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(RenderPassToggleGuardTest, NullRegistryAlwaysReturnsTrue)
{
    EXPECT_TRUE(ShouldDeclareBuiltInPassThisFrame(nullptr, "DrawSkyBackground"));
    EXPECT_TRUE(ShouldDeclareBuiltInPassThisFrame(nullptr, "AnyOtherPassName"));
}

TEST(RenderPassToggleGuardTest, RealRegistryWithNameEnabledReturnsTrue)
{
    RenderPassToggleRegistry registry;
    // An unknown name reads as enabled by default - see IsEnabled()'s own
    // header comment.
    EXPECT_TRUE(ShouldDeclareBuiltInPassThisFrame(&registry, "DrawSkyBackground"));
}

TEST(RenderPassToggleGuardTest, RealRegistryWithNameDisabledReturnsFalse)
{
    RenderPassToggleRegistry registry;
    ASSERT_TRUE(registry.SetEnabled("DrawSkyBackground", false));

    EXPECT_FALSE(ShouldDeclareBuiltInPassThisFrame(&registry, "DrawSkyBackground"));
}

TEST(RenderPassToggleGuardTest, CallingTwiceSameFrameForSameNameReturnsSameAnswer)
{
    // Mirrors ProviderScope::PerActiveView's own double-invocation shape
    // (once for Game View, once for Scene View, same frame, same debugName)
    // - this is the exact property that makes PHASE1's own "DrawSkyBackground"
    // fix correct: the guard must not somehow toggle itself off/flip-flop
    // between the Game View invocation and the Scene View invocation just
    // because it was already called once this frame.
    RenderPassToggleRegistry enabledRegistry;
    EXPECT_TRUE(ShouldDeclareBuiltInPassThisFrame(&enabledRegistry, "DrawSkyBackground"));
    EXPECT_TRUE(ShouldDeclareBuiltInPassThisFrame(&enabledRegistry, "DrawSkyBackground"));

    RenderPassToggleRegistry disabledRegistry;
    ASSERT_TRUE(disabledRegistry.SetEnabled("DrawSkyBackground", false));
    EXPECT_FALSE(ShouldDeclareBuiltInPassThisFrame(&disabledRegistry, "DrawSkyBackground"));
    EXPECT_FALSE(ShouldDeclareBuiltInPassThisFrame(&disabledRegistry, "DrawSkyBackground"));
}

} // namespace
} // namespace gte::rg
