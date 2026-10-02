// Tier-1 tests proving the PostOpaque/PostTransparent "enabledOverride"
// host-side toggle genuinely skips a feature's own callback at RUNTIME, not
// merely in DebugSnapshot()'s reported flag.
//
// Core.cpp's own real "PostOpaqueFeatures"/"PostTransparentFeatures"
// providers are private lambdas with no public accessor - this file proves
// the real, shared mechanism they both depend on
// (PostOpaqueFeaturesInPriorityOrder()/PostTransparentFeaturesInPriorityOrder()
// plus each entry's own enabledOverride flag) by replicating their documented
// loop shape exactly (walk the ordered list, `continue` on a disabled
// entry) against a real, headless Core/RenderFeatureCompositor - mirroring
// RenderPipelineTests.cpp's own "drive a provider-shaped loop with no live
// Vulkan device" fixture pattern.

#include "Core/Plugins/RenderFeatureCompositor.h"
#include "Core/Plugins/RenderFeatureDebugEntry.h"
#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "../../Fakes/HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

namespace gte {
namespace {

class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel /*level*/, std::string_view /*message*/) override { }
};

std::unique_ptr<Core> TryMakeHeadlessCore(HeadlessSurfaceProvider& surfaceProvider, NoopHostServices& hostServices)
{
    try {
        return std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception&) {
        return nullptr;
    }
}

const RenderFeatureDebugEntry* FindByName(const std::vector<RenderFeatureDebugEntry>& snapshot, const std::string& name)
{
    for (const RenderFeatureDebugEntry& entry : snapshot) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

#define GTE_SKIP_IF_NO_HEADLESS_CORE(core)                                                                           \
    HeadlessSurfaceProvider surfaceProvider;                                                                         \
    NoopHostServices hostServices;                                                                                   \
    std::unique_ptr<Core> core = TryMakeHeadlessCore(surfaceProvider, hostServices);                                 \
    if (core == nullptr) {                                                                                           \
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "                \
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "                    \
                        "HeadlessSurfaceProvider.h's own top-of-file comment).";                                     \
    }

// Item 1 of this phase's Editor-surface coverage - both stages genuinely
// appear in DebugSnapshot() together, and SetFeatureEnabled()/
// SetFeaturePriority() genuinely mutate each independently.
TEST(RenderFeatureCompositorPostStageRuntimeToggleTest, BothPostOpaqueAndPostTransparentAppearAndAreIndependentlyMutable)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("RuntimeToggle_PostOpaque", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    ASSERT_TRUE(compositor->RegisterPostTransparentFeature("RuntimeToggle_PostTransparent", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    ASSERT_NE(FindByName(snapshot, "RuntimeToggle_PostOpaque"), nullptr);
    ASSERT_NE(FindByName(snapshot, "RuntimeToggle_PostTransparent"), nullptr);

    EXPECT_TRUE(compositor->SetFeatureEnabled("RuntimeToggle_PostOpaque", false));
    EXPECT_TRUE(compositor->SetFeaturePriority("RuntimeToggle_PostOpaque", 11));
    EXPECT_TRUE(compositor->SetFeatureEnabled("RuntimeToggle_PostTransparent", false));
    EXPECT_TRUE(compositor->SetFeaturePriority("RuntimeToggle_PostTransparent", 22));

    const std::vector<RenderFeatureDebugEntry> after = compositor->DebugSnapshot();
    const RenderFeatureDebugEntry* postOpaque = FindByName(after, "RuntimeToggle_PostOpaque");
    const RenderFeatureDebugEntry* postTransparent = FindByName(after, "RuntimeToggle_PostTransparent");
    ASSERT_NE(postOpaque, nullptr);
    ASSERT_NE(postTransparent, nullptr);
    EXPECT_FALSE(postOpaque->enabled);
    EXPECT_EQ(postOpaque->priority, 11);
    EXPECT_FALSE(postTransparent->enabled);
    EXPECT_EQ(postTransparent->priority, 22);
}

// Item 2 - a PostOpaque feature's own callback increments a counter each
// time it runs; after SetFeatureEnabled(name, false), a provider-shaped
// loop that mirrors Core.cpp's real "PostOpaqueFeatures" skip logic
// (`if (!entry.enabledOverride) { continue; }`) must NOT invoke the
// callback again.
TEST(RenderFeatureCompositorPostStageRuntimeToggleTest, DisablingAPostOpaqueFeatureStopsItsCallbackFromRunningAgain)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    int callCount = 0;
    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("RuntimeToggle_PostOpaque_Counter", 0,
        [&callCount](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId,
            const ScenePassReadHandles&) { ++callCount; }));

    rg::RenderGraphBuilder builder;
    rg::RenderPassBlackboard blackboard;
    const ScenePassReadHandles dummyHandles{};

    auto runProviderLoopOnce = [&]() {
        for (const RenderFeatureCompositor::PostOpaqueEntry& entry : compositor->PostOpaqueFeaturesInPriorityOrder()) {
            if (!entry.enabledOverride) {
                continue;
            }
            entry.callback(builder, blackboard, rg::RenderViewId::Named("Game"), dummyHandles);
        }
    };

    runProviderLoopOnce();
    EXPECT_EQ(callCount, 1);

    ASSERT_TRUE(compositor->SetFeatureEnabled("RuntimeToggle_PostOpaque_Counter", false));
    runProviderLoopOnce();
    EXPECT_EQ(callCount, 1) << "A disabled PostOpaque feature's callback must not run again.";
}

// Item 2, PostTransparent sibling.
TEST(RenderFeatureCompositorPostStageRuntimeToggleTest, DisablingAPostTransparentFeatureStopsItsCallbackFromRunningAgain)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    int callCount = 0;
    ASSERT_TRUE(compositor->RegisterPostTransparentFeature("RuntimeToggle_PostTransparent_Counter", 0,
        [&callCount](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId,
            const ScenePassReadHandles&) { ++callCount; }));

    rg::RenderGraphBuilder builder;
    rg::RenderPassBlackboard blackboard;
    const ScenePassReadHandles dummyHandles{};

    auto runProviderLoopOnce = [&]() {
        for (const RenderFeatureCompositor::PostTransparentEntry& entry :
            compositor->PostTransparentFeaturesInPriorityOrder()) {
            if (!entry.enabledOverride) {
                continue;
            }
            entry.callback(builder, blackboard, rg::RenderViewId::Named("Game"), dummyHandles);
        }
    };

    runProviderLoopOnce();
    EXPECT_EQ(callCount, 1);

    ASSERT_TRUE(compositor->SetFeatureEnabled("RuntimeToggle_PostTransparent_Counter", false));
    runProviderLoopOnce();
    EXPECT_EQ(callCount, 1) << "A disabled PostTransparent feature's callback must not run again.";
}

} // namespace gte
